#pragma once
typedef enum { HBL_LAUNCH_OK,HBL_LAUNCH_BAD_FILE,HBL_LAUNCH_UNAVAILABLE,HBL_LAUNCH_NO_TITLE,HBL_LAUNCH_IPC } HblLaunchResult;
HblLaunchResult hbl_launch(const char *path);
