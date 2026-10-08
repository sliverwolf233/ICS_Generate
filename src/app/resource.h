// resource.h - resource and command identifiers for ICS Generate.
// ASCII only: the resource compiler reads this file with the system code page.
#pragma once
#ifndef ICSG_RESOURCE_H
#define ICSG_RESOURCE_H

// ------------------------------------------------------------- resources ----
#define IDI_APPICON         101

// -------------------------------------------------------------- commands ----
#define IDM_FILE_NEW        1001
#define IDM_FILE_OPEN       1002
#define IDM_FILE_MERGE      1003
#define IDM_FILE_SAVE       1004
#define IDM_FILE_SAVEAS     1005
#define IDM_FILE_EXPORT     1006
#define IDM_FILE_CLEAR      1007
#define IDM_FILE_EXIT       1008

#define IDM_EDIT_ADD        1010
#define IDM_EDIT_EDIT       1011
#define IDM_EDIT_DELETE     1012
#define IDM_EDIT_DUP        1013
#define IDM_EDIT_UP         1014
#define IDM_EDIT_DOWN       1015
#define IDM_EDIT_FIND       1016

#define IDM_HELP_HELP       1020
#define IDM_HELP_ABOUT      1021

// ------------------------------------------------------- main window parts ---
// The button row reuses the command identifiers so both the menu and the
// buttons produce exactly the same WM_COMMAND.
#define IDC_LIST            2001
#define IDC_FILTER          2002
#define IDC_STATUS          2003
#define IDC_LBL_FILTER      2004
#define IDC_LBL_HINT        2005

// ---------------------------------------------------------- event editor ----
#define IDC_ED_SUMMARY      3001
#define IDC_ED_LOCATION     3002
#define IDC_ED_DESC         3003
#define IDC_ED_URL          3004
#define IDC_ED_CATEGORIES   3005
#define IDC_ED_ORGNAME      3006
#define IDC_ED_ORGMAIL      3007
#define IDC_DTP_START       3010
#define IDC_DTP_END         3011
#define IDC_CHK_ALLDAY      3012
#define IDC_CB_TIMEMODE     3013
#define IDC_CB_TZID         3014
#define IDC_BTN_LOCALTZ     3015
#define IDC_BTN_TOUTC       3016
#define IDC_DTP_STARTTIME   3017
#define IDC_DTP_ENDTIME     3018
#define IDC_CB_STATUS       3020
#define IDC_CB_PRIORITY     3021
#define IDC_LB_ATTENDEES    3022
#define IDC_ED_ATTMAIL      3023
#define IDC_ED_ATTNAME      3024
#define IDC_BTN_ATTADD      3025
#define IDC_BTN_ATTREMOVE   3026
#define IDC_CHK_ALARM1      3030
#define IDC_CHK_ALARM2      3031
#define IDC_CHK_ALARM3      3032
#define IDC_CHK_ALARM4      3033
#define IDC_ED_ALARM1       3038
#define IDC_ED_ALARM2       3039
#define IDC_ED_ALARM3       3040
#define IDC_ED_ALARM4       3041
#define IDC_SPN_ALARM1      3034
#define IDC_SPN_ALARM2      3035
#define IDC_SPN_ALARM3      3036
#define IDC_SPN_ALARM4      3037
#define IDC_CB_FREQ         3040
#define IDC_SPN_INTERVAL    3041
#define IDC_SPN_MONTHDAY    3042
#define IDC_CB_ENDMODE      3043
#define IDC_SPN_COUNT       3044
#define IDC_DTP_UNTIL       3045
#define IDC_ED_INTERVAL     3046
#define IDC_ED_COUNT        3047
#define IDC_ED_MONTHDAY     3048
#define IDC_CHK_BYDAY_SU    3050
#define IDC_CHK_BYDAY_MO    3051
#define IDC_CHK_BYDAY_TU    3052
#define IDC_CHK_BYDAY_WE    3053
#define IDC_CHK_BYDAY_TH    3054
#define IDC_CHK_BYDAY_FR    3055
#define IDC_CHK_BYDAY_SA    3056
#define IDC_DTP_DATE        3060
#define IDC_BTN_EXADD       3061
#define IDC_BTN_RDADD       3062
#define IDC_BTN_DATEREMOVE  3063
#define IDC_LB_DATES        3064

#endif // ICSG_RESOURCE_H
