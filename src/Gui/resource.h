#pragma once

#define IDD_MAIN                101
#define IDI_APPICON             102
#define IDD_CAMSETTINGS         103

#define IDC_COUNT_COMBO         1001
#define IDC_START               1002
#define IDC_STOP                1003
#define IDC_STATUS              1004
#define IDC_REGISTER            1005
#define IDC_UNREGISTER          1006

// Per-camera rows: name edit + path label + browse/settings buttons, IDs
// offset by slot index (0-3).
#define IDC_PATH_BASE           1100 // 1100..1103
#define IDC_BROWSE_BASE         1110 // 1110..1113
#define IDC_ROWSTATUS_BASE      1120 // 1120..1123
#define IDC_NAME_BASE           1130 // 1130..1133
#define IDC_THUMB_BASE          1140 // 1140..1143
#define IDC_SETTINGS_BASE       1150 // 1150..1153

// IDD_CAMSETTINGS: per-camera resolution + slideshow modal (one instance,
// reused for whichever slot's "Settings..." button was clicked).
#define IDC_SET_RES_COMBO       1200
#define IDC_SET_SLIDESHOW_ENABLE 1201
#define IDC_SET_SLIDESHOW_FOLDER 1202
#define IDC_SET_SLIDESHOW_BROWSE 1203
#define IDC_SET_SLIDESHOW_INTERVAL 1204
