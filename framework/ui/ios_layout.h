#ifndef HACHIMI_IOS_LAYOUT_H
#define HACHIMI_IOS_LAYOUT_H

/* 800x480 iPad-style layout: leave 26 px clear below the 32 px status area. */
#define IOS_STATUS_BAR_CONTENT_BOTTOM 32
#define IOS_STATUS_BAR_SAFE_GAP 26
#define IOS_APP_CONTENT_SAFE_TOP (IOS_STATUS_BAR_CONTENT_BOTTOM + IOS_STATUS_BAR_SAFE_GAP)

/* Settings and Files content panels begin beneath the status bar strip. */
#define IOS_APP_CONTENT_PANEL_TOP 28
#define IOS_APP_CONTENT_PANEL_LOCAL_TOP (IOS_APP_CONTENT_SAFE_TOP - IOS_APP_CONTENT_PANEL_TOP)

#endif
