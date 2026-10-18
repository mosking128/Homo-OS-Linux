#!/usr/bin/python3
"""Board integration check. Run with sudo over the wired management connection.

Briefly disconnects Wi-Fi, exercises power and joins, then restores the exact
original saved configuration. Credentials are never printed.
"""
import os
import re
import runpy
import subprocess
import time

adapter = runpy.run_path('/usr/local/libexec/hachimi-network')
wpa = adapter['wpa']
unescape = adapter['unescape']
config_path = '/etc/wpa_supplicant/wpa_supplicant.conf'
original_config = open(config_path, 'rb').read()


def operation(name, *fields, success=True):
    result = subprocess.run(['/usr/local/libexec/hachimi-network'],
                            input='\t'.join((name,) + fields) + '\n',
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                            universal_newlines=True, timeout=65)
    assert (result.returncode == 0) == success, name + ' returned unexpected result'
    return result.stdout


def wait_connected():
    for _ in range(40):
        state = dict(line.split('=', 1) for line in wpa('status').splitlines() if '=' in line)
        if state.get('wpa_state') == 'COMPLETED' and state.get('ip_address'):
            return state
        time.sleep(1)
    raise AssertionError('Wi-Fi did not recover its association and IPv4 address')


try:
    original = wait_connected()
    original_id = original['id']
    original_ssid = unescape(original['ssid'])
    password = None
    for block in re.findall(r'network=\{(.*?)\}', original_config.decode('utf-8'), re.S):
        fields = dict(re.findall(r'^\s*(\w+)=(.*?)\s*$', block, re.M))
        value = fields.get('ssid', '')
        name = unescape(value[1:-1]) if value.startswith('"') else bytes.fromhex(value).decode('utf-8')
        if name == original_ssid and 'psk' in fields:
            password = fields['psk']
            password = unescape(password[1:-1]) if password.startswith('"') else password
            break
    assert password, 'Current network has no saved personal-network credentials'
    assert 'ERROR' in operation('unsupported-operation', success=False)
    print('PASS: unsupported requests rejected')
    result = operation('scan')
    assert 'AP\t' in result
    print('PASS: live Wi-Fi scan')
    operation('disconnect')
    assert 'wpa_state=COMPLETED' not in wpa('status')
    operation('reconnect')
    wait_connected()
    print('PASS: disconnect and reconnect')
    operation('wifi-off')
    assert operation('status').startswith('WIFI\t1\t0\t')
    operation('wifi-on')
    wait_connected()
    print('PASS: Wi-Fi power off/on and DHCP recovery')
    missing = 'Hachimi-check-absent-6fd24'
    operation('connect', missing.encode().hex(), 'validation-password'.encode().hex(), 'PSK', success=False)
    assert wait_connected()['ssid'] == original['ssid']
    assert open(config_path, 'rb').read() == original_config
    print('PASS: failed join restores prior connection without saving bad credentials')
    operation('connect', original_ssid.encode().hex(), password.encode().hex(), 'PSK')
    assert wait_connected()['ssid'] == original['ssid']
    print('PASS: password-based join, saved configuration and DHCP')
finally:
    # Restore all original profiles, their order and their flags, even after an assertion.
    with open(config_path, 'wb') as config:
        config.write(original_config)
    os.chmod(config_path, 0o600)
    subprocess.run(['systemctl', 'start', 'autowifi'], check=True)
    wpa('reconfigure')
    wpa('reconnect')
    wait_connected()
    assert open(config_path, 'rb').read() == original_config
    print('PASS: original configuration and connectivity restored')
