#if !defined(_ELAN_COMMON_H_)
#define _ELAN_COMMON_H_

//
//These are the device attributes returned by vmulti in response
// to IOCTL_HID_GET_DEVICE_ATTRIBUTES.
//

#define ELAN_PID              0xBACC
#define ELAN_VID              0x00FF
#define ELAN_VERSION          0x0001

//
// These are the report ids
//

#define REPORTID_MTOUCH         0x01
#define REPORTID_FEATURE        0x02
#define REPORTID_MOUSE			0x03
#define REPORTID_PTPHQA			0x04

//
// Multitouch specific report information
//

#define MULTI_CONFIDENCE_BIT   1
#define MULTI_TIPSWITCH_BIT    2

#define MULTI_MIN_COORDINATE   0x0000
#define MULTI_MAX_COORDINATE   0x7FFF

#define MULTI_MAX_COUNT        5

#pragma pack(1)
typedef struct
{

	BYTE	  Status;

	BYTE	  ContactID;

	USHORT    XValue;

	USHORT    YValue;

	USHORT	  Pressure;
}
TOUCH, *PTOUCH;

typedef struct _ELAN_MULTITOUCH_REPORT
{

	BYTE      ReportID;

	TOUCH      Touch[5];

	USHORT	  ScanTime;

	BYTE	  ContactCount;

	BYTE	  IsDepressed;
} ElanMultiTouchReport;
#pragma pack()

//
// Feature report infomation
//

#define DEVICE_MODE_MOUSE        0x00
#define DEVICE_MODE_SINGLE_INPUT 0x01
#define DEVICE_MODE_MULTI_INPUT  0x02

#pragma pack(1)
typedef struct _ELAN_FEATURE_REPORT
{

	BYTE      ReportID;

	BYTE      DeviceMode;

	BYTE      DeviceIdentifier;

} ElanFeatureReport;

typedef struct _ELAN_MAXCOUNT_REPORT
{

	BYTE         ReportID;

	BYTE		 MaximumCount;

	BYTE		 PadType;

} ElanMaxCountReport;
#pragma pack()

#endif
