#!/usr/bin/env python3
"""Build a bounded Beijing offline RGB565 XYZ map from OSM vector data.

Only the explicit --download step accesses the network. No public raster tile
server is scraped. The raw OSM extract, metadata and POI index accompany tiles.
Requires Pillow on the packaging computer; the device needs no Python.
"""
import argparse
import array
import json
import math
from pathlib import Path
import re
import struct
import sys
import time
import urllib.parse
import urllib.request
from PIL import Image, ImageDraw, ImageFont

BBOX = (39.86, 116.34, 39.95, 116.47)
CENTER = (39.9042, 116.4074)
ATTRIBUTION = '© OpenStreetMap contributors · ODbL 1.0'
DEFAULT_OUT = Path(__file__).resolve().parents[2] / 'custom/assets/maps/beijing'


def world(lat, lon, zoom):
    n = 256 * (1 << zoom)
    r = math.radians(lat)
    return (lon + 180) / 360 * n, (1 - math.asinh(math.tan(r)) / math.pi) / 2 * n


def download(path):
    south, west, north, east = BBOX
    box = ','.join(map(str, BBOX))
    query = f'''[out:json][timeout:100];(
way[highway~"^(motorway|trunk|primary|secondary|tertiary|unclassified|residential|living_street)(_link)?$"]({box});
way[waterway~"^(river|canal)$"]({box});
way[natural=water]({box});relation[natural=water]({box});
way[leisure~"^(park|garden)$"]({box});
way[landuse~"^(forest|grass|recreation_ground)$"]({box});
way[building](39.89,116.38,39.92,116.42);
nwr[name][tourism~"^(attraction|museum|hotel|viewpoint)$"]({box});
nwr[name][amenity~"^(restaurant|cafe|hospital|clinic|bus_station|university|library)$"]({box});
nwr[name][leisure=park]({box});
node[name][railway=station]({box});
);out geom;'''
    errors = []
    for endpoint in ('https://overpass.osm.jp/api/interpreter',
                     'https://maps.mail.ru/osm/tools/overpass/api/interpreter',
                     'https://overpass.private.coffee/api/interpreter',
                     'https://overpass-api.de/api/interpreter'):
        try:
            req = urllib.request.Request(endpoint+'?'+urllib.parse.urlencode({'data': query}),
                headers={'User-Agent': 'HachimiOfflineMap/1.0 (bounded Beijing extract)'})
            with urllib.request.urlopen(req, timeout=140) as response:
                data = response.read(24 * 1024 * 1024 + 1)
            if len(data) > 24 * 1024 * 1024:
                raise ValueError('Extract exceeds bounded size')
            parsed = json.loads(data)
            if parsed.get('remark') or not parsed.get('elements'):
                raise ValueError(parsed.get('remark', 'Empty response'))
            # Publish only geometry and tags consumed by this renderer/index.
            # Contact details and unrelated public OSM tags are not needed.
            keep = {'highway', 'name', 'name:zh', 'waterway', 'width', 'natural',
                    'leisure', 'landuse', 'building', 'tourism', 'amenity', 'railway'}
            for element in parsed['elements']:
                if 'tags' in element:
                    element['tags'] = {k: v for k, v in element['tags'].items() if k in keep}
            path.write_text(json.dumps(parsed, ensure_ascii=False, separators=(',', ':')), encoding='utf-8')
            print('Downloaded', len(data), 'bytes;', len(parsed['elements']), 'OSM elements', flush=True)
            return
        except Exception as exc:
            errors.append(str(exc))
    raise RuntimeError('OSM extract download failed: ' + '; '.join(errors))


def points(element):
    return [(p['lat'], p['lon']) for p in element.get('geometry', []) if 'lat' in p]


def midpoint(element):
    if 'lat' in element:
        return element['lat'], element['lon']
    pts = points(element)
    if not pts:
        pts = [p for member in element.get('members', []) for p in points(member)]
    if not pts:
        return None
    return sum(p[0] for p in pts) / len(pts), sum(p[1] for p in pts) / len(pts)


def category(tags):
    if tags.get('leisure') == 'park': return '公园'
    if tags.get('amenity') in ('restaurant', 'cafe'): return '餐饮'
    if tags.get('amenity') in ('hospital', 'clinic'): return '医疗'
    if tags.get('railway') == 'station' or tags.get('amenity') == 'bus_station': return '交通'
    if tags.get('tourism') in ('attraction', 'museum', 'viewpoint'): return '景点'
    if tags.get('tourism') == 'hotel': return '酒店'
    if tags.get('amenity') in ('university', 'library'): return '文化'
    return None


def pack(source, out, font_path):
    from pypinyin import lazy_pinyin
    elements = json.loads(source.read_text(encoding='utf-8'))['elements']
    places, seen = [], set()
    for e in elements:
        tags = e.get('tags', {})
        name = tags.get('name:zh', tags.get('name', ''))
        cat, pos = category(tags), midpoint(e)
        if not cat or not pos or not name or len(name.encode()) > 120: continue
        lat, lon = pos
        if not (BBOX[0] <= lat <= BBOX[2] and BBOX[1] <= lon <= BBOX[3]): continue
        key = (name, round(lat, 3), round(lon, 3))
        if key in seen: continue
        seen.add(key)
        places.append({'name': name, 'category': cat, 'lat': lat, 'lon': lon,
                       'osm': f"{e['type']}/{e['id']}"})
    places.sort(key=lambda p: (p['category'] != '景点', p['category'] != '公园', p['name']))
    if not places: raise ValueError('No real POIs in map extract')
    # TSV is deliberately simple to parse on the device; text is bounded and
    # newline/tab sanitized. Coordinates are WGS84 from the OSM source.
    with (out / 'places.tsv').open('w', encoding='utf-8', newline='\n') as f:
        for p in places[:512]:
            clean = lambda s: re.sub(r'[\t\r\n]', ' ', s)
            syllables=lazy_pinyin(p['name'])
            alias=''.join(syllables)+' '+''.join(s[0] for s in syllables if s)
            f.write(f"{p['lat']:.7f}\t{p['lon']:.7f}\t{clean(p['category'])}\t{clean(p['name'])}\t{clean(alias)}\n")
    font = lambda size: ImageFont.truetype(str(font_path), size)
    levels = []
    for z in (13, 14, 15, 16):
        nw, se = world(BBOX[2], BBOX[1], z), world(BBOX[0], BBOX[3], z)
        x0, y0 = int(nw[0] // 256), int(nw[1] // 256)
        x1, y1 = int(se[0] // 256), int(se[1] // 256)
        size = ((x1-x0+1)*256, (y1-y0+1)*256)
        image = Image.new('RGB', size, '#f4f2ed')
        draw = ImageDraw.Draw(image)
        xy = lambda pts: [(round(world(lat, lon, z)[0]-x0*256),
                           round(world(lat, lon, z)[1]-y0*256)) for lat, lon in pts]
        for e in elements:
            t, ps = e.get('tags', {}), points(e)
            color = None
            if t.get('leisure') in ('park', 'garden') or t.get('landuse') in ('forest', 'grass', 'recreation_ground'):
                color = '#cfe5c9'
            if t.get('building') and z >= 15: color = '#e4e1db'
            if t.get('natural') == 'water': color = '#a8d9ed'
            if color and len(ps) >= 3 and ps[0] == ps[-1]: draw.polygon(xy(ps), fill=color)
            if t.get('natural') == 'water' and e['type'] == 'relation':
                # Stitch OSM multipolygon outer members by their endpoints.
                rings = [points(m) for m in e.get('members', []) if m.get('role') == 'outer' and points(m)]
                while rings:
                    ring = rings.pop()
                    changed = True
                    while changed and ring[0] != ring[-1]:
                        changed = False
                        for i, part in enumerate(rings):
                            if ring[-1] == part[0]: ring += part[1:]
                            elif ring[-1] == part[-1]: ring += part[-2::-1]
                            elif ring[0] == part[-1]: ring = part[:-1] + ring
                            elif ring[0] == part[0]: ring = part[:0:-1] + ring
                            else: continue
                            rings.pop(i); changed = True; break
                    if len(ring)>3 and ring[0] == ring[-1]: draw.polygon(xy(ring), fill='#a8d9ed')
        for e in elements:
            ps, t = points(e), e.get('tags', {})
            if len(ps) < 2: continue
            if t.get('waterway') in ('river', 'canal'):
                try: meters = float(t.get('width', '70').split()[0])
                except ValueError: meters = 70
                mpp = 156543.03392 * math.cos(math.radians(CENTER[0])) / (1 << z)
                draw.line(xy(ps), fill='#a8d9ed', width=max(2, int(min(meters,400)/mpp)), joint='curve')
        roads = [e for e in elements if 'highway' in e.get('tags', {}) and len(points(e))>1]
        rank = lambda e: {'motorway':5,'trunk':4,'primary':3,'secondary':2,'tertiary':1}.get(e['tags']['highway'].replace('_link',''),0)
        roads.sort(key=rank)
        for casing in (True, False):
            for e in roads:
                r = rank(e)
                width = max(2, (z-12) + r)
                if z==13 and r==0: width=1
                color = '#dedbd4' if casing else ('#f9d997' if r>=3 else '#ffffff')
                draw.line(xy(points(e)), fill=color, width=width+(2 if casing else 0), joint='curve')
        # Sparse, collision-aware labels keep the light basemap legible.
        occupied=[]
        def text(pos, name, color, size=13):
            if not name or len(name)>18: return
            f=font(size); box=draw.textbbox(pos,name,font=f,anchor='mm',stroke_width=1)
            if box[0]<8 or box[1]<8 or box[2]>=image.width-8 or box[3]>=image.height-8: return
            if any(not(box[2]+10<b[0] or box[0]-10>b[2] or box[3]+8<b[1] or box[1]-8>b[3]) for b in occupied): return
            draw.text(pos,name,font=f,fill=color,anchor='mm',stroke_width=2,stroke_fill='#f8f7f2')
            occupied.append(box)
        for e in sorted(roads,key=rank,reverse=True):
            name=e['tags'].get('name:zh',e['tags'].get('name',''))
            ps=points(e)
            if (rank(e)>1 or z>=15) and len(ps)>2: text(xy([ps[len(ps)//2]])[0],name,'#85817a',12)
        for p in places:
            px,py=xy([(p['lat'],p['lon'])])[0]
            color='#5b9d6b' if p['category']=='公园' else '#7a8f9b'
            draw.ellipse((px-3,py-3,px+3,py+3),fill=color)
            if z>=15 or p['category'] in ('公园','景点'): text((px,py+14),p['name'],color,13)
        # Convert once on the PC; runtime only reads ready-to-blit RGB565.
        for tx in range(x0,x1+1):
            folder=out/'tiles'/str(z)/str(tx);folder.mkdir(parents=True,exist_ok=True)
            for ty in range(y0,y1+1):
                tile=image.crop(((tx-x0)*256,(ty-y0)*256,(tx-x0+1)*256,(ty-y0+1)*256))
                raw=tile.tobytes()
                pixels=array.array('H',(((raw[i]&248)<<8)|((raw[i+1]&252)<<3)|(raw[i+2]>>3) for i in range(0,len(raw),3)))
                if sys.byteorder!='little': pixels.byteswap()
                (folder/f'{ty}.rgb565').write_bytes(pixels.tobytes())
        levels.append((z,x0,y0,x1,y1))
        print('Packed zoom',z, size, (x1-x0+1)*(y1-y0+1),'tiles',flush=True)
    meta={'region':'北京中心城区','bbox':BBOX,'center':CENTER,'zooms':[13,14,15,16],
          'levels':levels,'tile_size':256,'format':'RGB565 little-endian','places':len(places[:512]),
          'attribution':ATTRIBUTION,'source':'https://www.openstreetmap.org',
          'license':'https://opendatacommons.org/licenses/odbl/1-0/','built_utc':time.strftime('%Y-%m-%dT%H:%M:%SZ',time.gmtime())}
    (out/'metadata.json').write_text(json.dumps(meta,ensure_ascii=False,indent=2),encoding='utf-8')
    with (out/'region.txt').open('w',encoding='utf-8',newline='\n') as f:
        f.write('HACHIMI_MAP_V1\n北京中心城区\n')
        f.write('%.7f %.7f %.7f %.7f\n'%BBOX)
        f.write('%.7f %.7f\n'%CENTER)
        for level in levels: f.write('%d %d %d %d %d\n'%level)
    (out/'ATTRIBUTION.txt').write_text(ATTRIBUTION+'\nhttps://www.openstreetmap.org/copyright\n'
        'Data: Open Database License 1.0. Filtered geometry extract: source.osm.json.\n'
        'Basemap rendering: Hachimi, generated locally from OSM geometry, not Apple map tiles.\n',encoding='utf-8')
    print('POIs:',len(places[:512]),flush=True)


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--download',action='store_true')
    p.add_argument('--out',type=Path,default=DEFAULT_OUT)
    p.add_argument('--font',type=Path,default=Path(__file__).resolve().parents[2] / 'custom/assets/ios/PingFang.ttf')
    p.add_argument('--download-only',action='store_true')
    args=p.parse_args();args.out.mkdir(parents=True,exist_ok=True)
    source=args.out/'source.osm.json'
    if args.download: download(source)
    if not args.download_only: pack(source,args.out,args.font)
