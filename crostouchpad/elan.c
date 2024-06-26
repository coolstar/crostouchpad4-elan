#define DESCRIPTOR_DEF
#include "driver.h"

#define bool int

static ULONG ElanDebugLevel = 100;
static ULONG ElanDebugCatagories = DBG_INIT || DBG_PNP || DBG_IOCTL;

static bool deviceLoaded = false;

NTSTATUS
DriverEntry(
__in PDRIVER_OBJECT  DriverObject,
__in PUNICODE_STRING RegistryPath
)
{
	NTSTATUS               status = STATUS_SUCCESS;
	WDF_DRIVER_CONFIG      config;
	WDF_OBJECT_ATTRIBUTES  attributes;

	ElanPrint(DEBUG_LEVEL_INFO, DBG_INIT,
		"Driver Entry\n");

	WDF_DRIVER_CONFIG_INIT(&config, ElanEvtDeviceAdd);

	WDF_OBJECT_ATTRIBUTES_INIT(&attributes);

	//
	// Create a framework driver object to represent our driver.
	//

	status = WdfDriverCreate(DriverObject,
		RegistryPath,
		&attributes,
		&config,
		WDF_NO_HANDLE
		);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_INIT,
			"WdfDriverCreate failed with status 0x%x\n", status);
	}

	return status;
}

NTSTATUS elan_i2c_read_cmd(PELAN_CONTEXT pDevice, UINT16 reg, uint8_t* val) {
	return SpbXferDataSynchronously(&pDevice->I2CContext, &reg, sizeof(UINT16), val, ETP_I2C_INF_LENGTH);
}

NTSTATUS elan_i2c_read_block(PELAN_CONTEXT pDevice, UINT16 reg, PVOID val, ULONG len) {
	return SpbXferDataSynchronously(&pDevice->I2CContext, &reg, sizeof(UINT16), val, len);
}

NTSTATUS elan_i2c_write_cmd(PELAN_CONTEXT pDevice, UINT16 reg, UINT16 cmd) {
	uint16_t buffer[] = { reg, cmd };
	return SpbWriteDataSynchronously(&pDevice->I2CContext, buffer, sizeof(buffer));
}

NTSTATUS elan_i2c_power_control(PELAN_CONTEXT pDevice, bool enable)
{
	uint8_t val[2];
	uint16_t reg;
	NTSTATUS status;

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_POWER_CMD, val);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	reg = *((uint16_t *)val);
	if (enable)
		reg &= ~ETP_DISABLE_POWER;
	else
		reg |= ETP_DISABLE_POWER;

	return elan_i2c_write_cmd(pDevice, ETP_I2C_POWER_CMD, reg);
}

static bool elan_check_ASUS_special_fw(uint8_t prodid, uint8_t ic_type)
{
	if (ic_type == 0x08 && prodid == 0x26)
		return true;
	if (ic_type != 0x0E)
		return false;

	switch (prodid) {
	case 0x05:
	case 0x06:
	case 0x07:
	case 0x09:
	case 0x13:
		return true;
	default:
		return false;
	}
}

NTSTATUS BOOTTRACKPAD(
	_In_  PELAN_CONTEXT  pDevice
	)
{
	NTSTATUS status;

	status = elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_RESET);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	/* Wait for device to reset */
	LARGE_INTEGER delay;
	delay.QuadPart = -100 * 10;
	KeDelayExecutionThread(KernelMode, FALSE, &delay);

	/* get reset achknowledgement 000 */
	uint8_t val[256];
	status = SpbReadDataSynchronously(&pDevice->I2CContext, val, ETP_I2C_INF_LENGTH);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = elan_i2c_read_block(pDevice, ETP_I2C_DESC_CMD, val, ETP_I2C_DESC_LENGTH);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = elan_i2c_read_block(pDevice, ETP_I2C_REPORT_DESC_CMD, val, ETP_I2C_REPORT_DESC_LENGTH);
	if (!NT_SUCCESS(status)) {
		return status;
	}


	status = elan_i2c_power_control(pDevice, 1);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	uint8_t val2[3];

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_UNIQUEID_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	uint8_t prodid = val2[0];

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_SM_VERSION_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	//uint8_t smvers = val2[0];
	uint8_t ictype = val2[1];

	if (elan_check_ASUS_special_fw(prodid, ictype)) { //some Elan trackpads on certain ASUS laptops are buggy (linux commit 2de4fcc64685def3e586856a2dc636df44532395)
		status = elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);
		if (!NT_SUCCESS(status)) {
			return status;
		}

		delay.QuadPart = -200 * 10;
		KeDelayExecutionThread(KernelMode, FALSE, &delay); //Wait for touchpad to wake up

		status = elan_i2c_write_cmd(pDevice, ETP_I2C_SET_CMD, ETP_ENABLE_ABS);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}
	else {
		status = elan_i2c_write_cmd(pDevice, ETP_I2C_SET_CMD, ETP_ENABLE_ABS);
		if (!NT_SUCCESS(status)) {
			return status;
		}

		status = elan_i2c_write_cmd(pDevice, ETP_I2C_STAND_CMD, ETP_I2C_WAKE_UP);
		if (!NT_SUCCESS(status)) {
			return status;
		}
	}

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_FW_VERSION_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	//uint8_t version = val2[0];

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_FW_CHECKSUM_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	//uint16_t csum = *((uint16_t *)val2);

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_IAP_VERSION_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	//uint8_t iapversion = val2[0];

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_PRESSURE_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_MAX_X_AXIS_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	pDevice->max_x = (*((uint16_t *)val2)) & 0x0fff;

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_MAX_Y_AXIS_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}
	pDevice->max_y = (*((uint16_t *)val2)) & 0x0fff;

	status = elan_i2c_read_cmd(pDevice, ETP_I2C_RESOLUTION_CMD, val2);
	if (!NT_SUCCESS(status)) {
		return status;
	}

	uint8_t hw_res_x = val2[0];
	uint8_t hw_res_y = val2[1];

	uint16_t dots_per_mm_x = (hw_res_x * 10 + 790) * 10 / 254;
	uint16_t dots_per_mm_y = (hw_res_y * 10 + 790) * 10 / 254;

	pDevice->phy_x = pDevice->max_x / dots_per_mm_x;
	pDevice->phy_y = pDevice->max_y / dots_per_mm_y;

	uint16_t rmax_x = pDevice->max_x * 1;
	rmax_x /= 1;

	uint16_t rmax_y = pDevice->max_y * 1;
	rmax_y /= 1;

	uint16_t max_x[] = { rmax_x };
	uint16_t max_y[] = { rmax_y };

	uint8_t *max_x8bit = (uint8_t *)max_x;
	uint8_t *max_y8bit = (uint8_t *)max_y;

	pDevice->max_x_hid[0] = max_x8bit[0];
	pDevice->max_x_hid[1] = max_x8bit[1];

	pDevice->max_y_hid[0] = max_y8bit[0];
	pDevice->max_y_hid[1] = max_y8bit[1];

	//DbgPrint("[elantp] Max: %d x %d; X: 0x%x 0x%x Y: 0x%x 0x%x\n", rmax_x, rmax_y, max_x8bit[0], max_x8bit[1], max_y8bit[0], max_y8bit[1]);


	uint16_t phy_x[] = { pDevice->phy_x * 10 };
	uint16_t phy_y[] = { pDevice->phy_y * 10 };

	uint8_t *phy_x8bit = (uint8_t *)phy_x;
	uint8_t *phy_y8bit = (uint8_t *)phy_y;

	pDevice->phy_x_hid[0] = phy_x8bit[0];
	pDevice->phy_x_hid[1] = phy_x8bit[1];

	pDevice->phy_y_hid[0] = phy_y8bit[0];
	pDevice->phy_y_hid[1] = phy_y8bit[1];

	pDevice->TrackpadBooted = true;

	return status;
}

NTSTATUS
OnPrepareHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesRaw,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

This routine caches the SPB resource connection ID.

Arguments:

FxDevice - a handle to the framework device object
FxResourcesRaw - list of translated hardware resources that
the PnP manager has assigned to the device
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PELAN_CONTEXT pDevice = GetDeviceContext(FxDevice);
	BOOLEAN fSpbResourceFound = FALSE;
	NTSTATUS status = STATUS_INSUFFICIENT_RESOURCES;

	UNREFERENCED_PARAMETER(FxResourcesRaw);

	//
	// Parse the peripheral's resources.
	//

	ULONG resourceCount = WdfCmResourceListGetCount(FxResourcesTranslated);

	for (ULONG i = 0; i < resourceCount; i++)
	{
		PCM_PARTIAL_RESOURCE_DESCRIPTOR pDescriptor;
		UCHAR Class;
		UCHAR Type;

		pDescriptor = WdfCmResourceListGetDescriptor(
			FxResourcesTranslated, i);

		switch (pDescriptor->Type)
		{
		case CmResourceTypeConnection:
			//
			// Look for I2C or SPI resource and save connection ID.
			//
			Class = pDescriptor->u.Connection.Class;
			Type = pDescriptor->u.Connection.Type;
			if (Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL &&
				Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
			{
				if (fSpbResourceFound == FALSE)
				{
					status = STATUS_SUCCESS;
					pDevice->I2CContext.I2cResHubId.LowPart = pDescriptor->u.Connection.IdLowPart;
					pDevice->I2CContext.I2cResHubId.HighPart = pDescriptor->u.Connection.IdHighPart;
					fSpbResourceFound = TRUE;
				}
				else
				{
				}
			}
			break;
		default:
			//
			// Ignoring all other resource types.
			//
			break;
		}
	}

	//
	// An SPB resource is required.
	//

	if (fSpbResourceFound == FALSE)
	{
		status = STATUS_NOT_FOUND;
	}

	status = SpbTargetInitialize(FxDevice, &pDevice->I2CContext);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	return status;
}

NTSTATUS
OnReleaseHardware(
_In_  WDFDEVICE     FxDevice,
_In_  WDFCMRESLIST  FxResourcesTranslated
)
/*++

Routine Description:

Arguments:

FxDevice - a handle to the framework device object
FxResourcesTranslated - list of raw hardware resources that
the PnP manager has assigned to the device

Return Value:

Status

--*/
{
	PELAN_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status = STATUS_SUCCESS;

	UNREFERENCED_PARAMETER(FxResourcesTranslated);

	SpbTargetDeinitialize(FxDevice, &pDevice->I2CContext);

	return status;
}

NTSTATUS
OnD0Entry(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine allocates objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	PELAN_CONTEXT pDevice = GetDeviceContext(FxDevice);
	NTSTATUS status;

	for (int i = 0; i < 5; i++){
		pDevice->Flags[i] = 0;
	}

	status = BOOTTRACKPAD(pDevice);
	if (!NT_SUCCESS(status))
	{
		return status;
	}

	pDevice->ConnectInterrupt = true;
	pDevice->RegsSet = false;

	ElanCompleteIdleIrp(pDevice);

	return status;
}

NTSTATUS
OnD0Exit(
_In_  WDFDEVICE               FxDevice,
_In_  WDF_POWER_DEVICE_STATE  FxPreviousState
)
/*++

Routine Description:

This routine destroys objects needed by the driver.

Arguments:

FxDevice - a handle to the framework device object
FxPreviousState - previous power state

Return Value:

Status

--*/
{
	UNREFERENCED_PARAMETER(FxPreviousState);

	NTSTATUS status;

	PELAN_CONTEXT pDevice = GetDeviceContext(FxDevice);

	status = elan_i2c_power_control(pDevice, 0);

	pDevice->ConnectInterrupt = false;

	return status;
}

BOOLEAN OnInterruptIsr(
	WDFINTERRUPT Interrupt,
	ULONG MessageID){
	UNREFERENCED_PARAMETER(MessageID);

	WDFDEVICE Device = WdfInterruptGetDevice(Interrupt);
	PELAN_CONTEXT pDevice = GetDeviceContext(Device);

	if (!pDevice->ConnectInterrupt)
		return false;
	if (!pDevice->TrackpadBooted)
		return false;

	LARGE_INTEGER CurrentTime;

	KeQuerySystemTime(&CurrentTime);

	LARGE_INTEGER DIFF;

	DIFF.QuadPart = 0;

	if (pDevice->LastTime.QuadPart != 0)
		DIFF.QuadPart = (CurrentTime.QuadPart - pDevice->LastTime.QuadPart) / 1000;

	uint8_t touchpadReport[ETP_MAX_REPORT_LEN];
	if (!NT_SUCCESS(SpbReadDataSynchronously(&pDevice->I2CContext, &touchpadReport, sizeof(touchpadReport)))) {
		return false;
	}

	if (touchpadReport[0] == 0xff) {
		return false;
	}

	uint8_t tp_report_id = touchpadReport[ETP_REPORT_ID_OFFSET];
	if (tp_report_id == ETP_TP_REPORT_ID || tp_report_id == ETP_TP_REPORT_ID2) {
		//Trackpoint

		uint8_t* packet = &touchpadReport[ETP_REPORT_ID_OFFSET + 1];

		struct _ELAN_TRACKPOINT_REPORT report = { 0 };
		report.ReportID = REPORTID_MOUSE;
		if (packet[0] & 0x1)
			report.Button |= 1; //Left Button
		if (packet[0] & 0x2)
			report.Button |= 2; //Right Button
		if (packet[0] & 0x4)
			report.Button |= 4; //Middle Button

		if ((packet[3] & 0x0F) == 0x06) {
			report.XValue = packet[4] - (int)((packet[1] ^ 0x80) << 1);
			report.YValue = (int)((packet[2] ^ 0x80) << 1) - packet[5];
		}

		size_t bytesWritten;
		ElanProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);

		return true;
	}

	struct _ELAN_MULTITOUCH_REPORT report;
	report.ReportID = REPORTID_MTOUCH;
	
	int x[5];
	int y[5];
	int p[5];
	int palm[5];
	for (int i = 0; i < 5; i++) {
		x[i] = -1;
		y[i] = -1;
		p[i] = -1;
		palm[i] = 0;
	}

	uint8_t *finger_data = &touchpadReport[ETP_FINGER_DATA_OFFSET];
	uint8_t tp_info = touchpadReport[ETP_TOUCH_INFO_OFFSET];
	uint8_t hover_info = touchpadReport[ETP_HOVER_INFO_OFFSET];
	bool contact_valid, hover_event;

	hover_event = hover_info & 0x40;
	for (int i = 0; i < ETP_MAX_FINGERS; i++) {
		contact_valid = tp_info & (1U << (3 + i));
		unsigned int pos_x, pos_y;
		unsigned int pressure, mk_x, mk_y;
		unsigned int area_x, area_y, major, minor;
		unsigned int scaled_pressure;

		if (contact_valid) {
			if (tp_report_id == ETP_REPORT_ID2) { //High Precision
				pos_x = (finger_data[0] << 8) | finger_data[1];
				pos_y = (finger_data[2] << 8) | finger_data[3];
			}
			else {
				pos_x = ((finger_data[0] & 0xf0) << 4) |
					finger_data[1];
				pos_y = ((finger_data[0] & 0x0f) << 8) |
					finger_data[2];
			}

			mk_x = (finger_data[3] & 0x0f);
			mk_y = (finger_data[3] >> 4);
			pressure = finger_data[4];

			pos_y = pDevice->max_y - pos_y;

			pos_x *= 1;
			pos_x /= 1;

			pos_y *= 1;
			pos_y /= 1;

			/*
			* To avoid treating large finger as palm, let's reduce the
			* width x and y per trace.
			*/
			area_x = mk_x;
			area_y = mk_y;

			major = max(area_x, area_y);
			minor = min(area_x, area_y);

			if (minor >= 7) {
				palm[i] = 1;
			}

			scaled_pressure = pressure;

			if (scaled_pressure > ETP_MAX_PRESSURE)
				scaled_pressure = ETP_MAX_PRESSURE;
			x[i] = pos_x;
			y[i] = pos_y;
			p[i] = scaled_pressure;
		}
		else {
		}

		if (contact_valid) {
			finger_data += ETP_FINGER_DATA_LEN;
		}
	}

	for (int i = 0; i < 5; i++) {
		if (pDevice->Flags[i] == MXT_T9_DETECT && x[i] == -1) {
			pDevice->Flags[i] = MXT_T9_RELEASE;
		}
		if (x[i] != -1) {
			pDevice->Flags[i] = MXT_T9_DETECT;

			pDevice->XValue[i] = (USHORT)x[i];
			pDevice->YValue[i] = (USHORT)y[i];
			pDevice->Palm[i] = (USHORT)palm[i];
			pDevice->PValue[i] = (USHORT)p[i];

			//DbgPrint("[elantp] %d: %dx%d", i, x[i], y[i]);
		}
	}

	pDevice->BUTTONPRESSED = (tp_info & 0x01);

	pDevice->TIMEINT += (USHORT)DIFF.QuadPart;

	pDevice->LastTime = CurrentTime;

	BYTE count = 0, i = 0;
	while (count < 5 && i < 5) {
		if (pDevice->Flags[i] != 0) {
			report.Touch[count].ContactID = i;

			report.Touch[count].XValue = pDevice->XValue[i];
			report.Touch[count].YValue = pDevice->YValue[i];
			report.Touch[count].Pressure = pDevice->PValue[i];

			//DbgPrint("[elantp] %d 2: %dx%d", i, report.Touch[count].XValue, report.Touch[count].YValue);

			uint8_t flags = pDevice->Flags[i];
			if (flags & MXT_T9_DETECT) {
				if (pDevice->Palm[i])
					report.Touch[count].Status = MULTI_TIPSWITCH_BIT;
				else
					report.Touch[count].Status = MULTI_CONFIDENCE_BIT | MULTI_TIPSWITCH_BIT;
			}
			else if (flags & MXT_T9_PRESS) {
				if (pDevice->Palm[i])
					report.Touch[count].Status = MULTI_TIPSWITCH_BIT;
				else
					report.Touch[count].Status = MULTI_CONFIDENCE_BIT | MULTI_TIPSWITCH_BIT;
			}
			else if (flags & MXT_T9_RELEASE) {
				report.Touch[count].Status = MULTI_CONFIDENCE_BIT;
				pDevice->Flags[i] = 0;
			}
			else
				report.Touch[count].Status = 0;

			count++;
		}
		i++;
	}

	report.ScanTime = pDevice->TIMEINT;
	report.IsDepressed = pDevice->BUTTONPRESSED;

	report.ContactCount = count;

	size_t bytesWritten;
	ElanProcessVendorReport(pDevice, &report, sizeof(report), &bytesWritten);

	pDevice->RegsSet = true;
	return true;
}

NTSTATUS
ElanEvtDeviceAdd(
IN WDFDRIVER       Driver,
IN PWDFDEVICE_INIT DeviceInit
)
{
	NTSTATUS                      status = STATUS_SUCCESS;
	WDF_IO_QUEUE_CONFIG           queueConfig;
	WDF_OBJECT_ATTRIBUTES         attributes;
	WDFDEVICE                     device;
	WDF_INTERRUPT_CONFIG interruptConfig;
	WDFQUEUE                      queue;
	PELAN_CONTEXT               devContext;

	UNREFERENCED_PARAMETER(Driver);

	PAGED_CODE();

	ElanPrint(DEBUG_LEVEL_INFO, DBG_PNP,
		"ElanEvtDeviceAdd called\n");

	//
	// Tell framework this is a filter driver. Filter drivers by default are  
	// not power policy owners. This works well for this driver because
	// HIDclass driver is the power policy owner for HID minidrivers.
	//

	WdfFdoInitSetFilter(DeviceInit);

	{
		WDF_PNPPOWER_EVENT_CALLBACKS pnpCallbacks;
		WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnpCallbacks);

		pnpCallbacks.EvtDevicePrepareHardware = OnPrepareHardware;
		pnpCallbacks.EvtDeviceReleaseHardware = OnReleaseHardware;
		pnpCallbacks.EvtDeviceD0Entry = OnD0Entry;
		pnpCallbacks.EvtDeviceD0Exit = OnD0Exit;

		WdfDeviceInitSetPnpPowerEventCallbacks(DeviceInit, &pnpCallbacks);
	}

	//
	// Setup the device context
	//

	WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes, ELAN_CONTEXT);

	//
	// Create a framework device object.This call will in turn create
	// a WDM device object, attach to the lower stack, and set the
	// appropriate flags and attributes.
	//

	status = WdfDeviceCreate(&DeviceInit, &attributes, &device);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfDeviceCreate failed with status code 0x%x\n", status);

		return status;
	}

	{
		WDF_DEVICE_STATE deviceState;
		WDF_DEVICE_STATE_INIT(&deviceState);

		deviceState.NotDisableable = WdfFalse;
		WdfDeviceSetDeviceState(device, &deviceState);
	}

	WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&queueConfig, WdfIoQueueDispatchParallel);

	queueConfig.EvtIoInternalDeviceControl = ElanEvtInternalDeviceControl;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&queue
		);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of hid report read requests
	//

	devContext = GetDeviceContext(device);

	devContext->FxDevice = device;
	devContext->TrackpadBooted = false;

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->ReportQueue
		);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create manual I/O queue to take care of idle power requests
	//

	WDF_IO_QUEUE_CONFIG_INIT(&queueConfig, WdfIoQueueDispatchManual);

	queueConfig.PowerManaged = WdfFalse;

	status = WdfIoQueueCreate(device,
		&queueConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->IdleQueue
	);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"WdfIoQueueCreate failed 0x%x\n", status);

		return status;
	}

	//
	// Create an interrupt object for hardware notifications
	//
	WDF_INTERRUPT_CONFIG_INIT(
		&interruptConfig,
		OnInterruptIsr,
		NULL);
	interruptConfig.PassiveHandling = TRUE;

	status = WdfInterruptCreate(
		device,
		&interruptConfig,
		WDF_NO_OBJECT_ATTRIBUTES,
		&devContext->Interrupt);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_PNP,
			"Error creating WDF interrupt object - %!STATUS!",
			status);

		return status;
	}

	//
	// Initialize DeviceMode
	//

	devContext->DeviceMode = DEVICE_MODE_MOUSE;

	return status;
}

void
ElanIdleIrpWorkItem
(
	IN WDFWORKITEM IdleWorkItem
)
{
	NTSTATUS status;
	PIDLE_WORKITEM_CONTEXT idleWorkItemContext;
	PELAN_CONTEXT deviceContext;
	PHID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO idleCallbackInfo;

	idleWorkItemContext = GetIdleWorkItemContext(IdleWorkItem);
	NT_ASSERT(idleWorkItemContext != NULL);

	deviceContext = GetDeviceContext(idleWorkItemContext->FxDevice);
	NT_ASSERT(deviceContext != NULL);

	//
	// Get the idle callback info from the workitem context
	//
	PIRP irp = WdfRequestWdmGetIrp(idleWorkItemContext->FxRequest);
	PIO_STACK_LOCATION stackLocation = IoGetCurrentIrpStackLocation(irp);

	idleCallbackInfo = (PHID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO)
		(stackLocation->Parameters.DeviceIoControl.Type3InputBuffer);

	//
	// idleCallbackInfo is validated already, so invoke idle callback
	//
	idleCallbackInfo->IdleCallback(idleCallbackInfo->IdleContext);

	//
	// Park this request in our IdleQueue and mark it as pending
	// This way if the IRP was cancelled, WDF will cancel it for us
	//
	status = WdfRequestForwardToIoQueue(
		idleWorkItemContext->FxRequest,
		deviceContext->IdleQueue);

	if (!NT_SUCCESS(status))
	{
		//
		// IdleQueue is a manual-dispatch, non-power-managed queue. This should
		// *never* fail.
		//

		NT_ASSERTMSG("WdfRequestForwardToIoQueue to IdleQueue failed!", FALSE);

		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Error forwarding idle notification Request:0x%p to IdleQueue:0x%p - %!STATUS!",
			idleWorkItemContext->FxRequest,
			deviceContext->IdleQueue,
			status);

		//
		// Complete the request if we couldnt forward to the Idle Queue
		//
		WdfRequestComplete(idleWorkItemContext->FxRequest, status);
	}
	else
	{
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Forwarded idle notification Request:0x%p to IdleQueue:0x%p - %!STATUS!",
			idleWorkItemContext->FxRequest,
			deviceContext->IdleQueue,
			status);
	}

	//
	// Delete the workitem since we're done with it
	//
	WdfObjectDelete(IdleWorkItem);

	return;
}

NTSTATUS
ElanProcessIdleRequest(
	IN PELAN_CONTEXT pDevice,
	IN WDFREQUEST Request,
	OUT BOOLEAN* Complete
)
{
	PHID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO idleCallbackInfo;
	PIRP irp;
	PIO_STACK_LOCATION irpSp;
	NTSTATUS status;

	NT_ASSERT(Complete != NULL);
	*Complete = TRUE;

	//
	// Retrieve request parameters and validate
	//
	irp = WdfRequestWdmGetIrp(Request);
	irpSp = IoGetCurrentIrpStackLocation(irp);

	if (irpSp->Parameters.DeviceIoControl.InputBufferLength <
		sizeof(HID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO))
	{
		status = STATUS_INVALID_BUFFER_SIZE;

		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Error: Input buffer is too small to process idle request - %!STATUS!",
			status);

		goto exit;
	}

	//
	// Grab the callback
	//
	idleCallbackInfo = (PHID_SUBMIT_IDLE_NOTIFICATION_CALLBACK_INFO)
		irpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	NT_ASSERT(idleCallbackInfo != NULL);

	if (idleCallbackInfo == NULL || idleCallbackInfo->IdleCallback == NULL)
	{
		status = STATUS_NO_CALLBACK_ACTIVE;
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Error: Idle Notification request %p has no idle callback info - %!STATUS!",
			Request,
			status);
		goto exit;
	}

	{
		//
		// Create a workitem for the idle callback
		//
		WDF_OBJECT_ATTRIBUTES workItemAttributes;
		WDF_WORKITEM_CONFIG workitemConfig;
		WDFWORKITEM idleWorkItem;
		PIDLE_WORKITEM_CONTEXT idleWorkItemContext;

		WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&workItemAttributes, IDLE_WORKITEM_CONTEXT);
		workItemAttributes.ParentObject = pDevice->FxDevice;

		WDF_WORKITEM_CONFIG_INIT(&workitemConfig, ElanIdleIrpWorkItem);

		status = WdfWorkItemCreate(
			&workitemConfig,
			&workItemAttributes,
			&idleWorkItem
		);

		if (!NT_SUCCESS(status)) {
			ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"Error creating creating idle work item - %!STATUS!",
				status);
			goto exit;
		}

		//
		// Set the workitem context
		//
		idleWorkItemContext = GetIdleWorkItemContext(idleWorkItem);
		idleWorkItemContext->FxDevice = pDevice->FxDevice;
		idleWorkItemContext->FxRequest = Request;

		//
		// Enqueue a workitem for the idle callback
		//
		WdfWorkItemEnqueue(idleWorkItem);

		//
		// Mark the request as pending so that 
		// we can complete it when we come out of idle
		//
		*Complete = FALSE;
	}

exit:

	return status;
}

VOID
ElanCompleteIdleIrp(
	IN PELAN_CONTEXT FxDeviceContext
)
/*++

Routine Description:

	This is invoked when we enter D0.
	We simply complete the Idle Irp if it hasn't been cancelled already.

Arguments:

	FxDeviceContext -  Pointer to Device Context for the device

Return Value:



--*/
{
	NTSTATUS status;
	WDFREQUEST request = NULL;

	//
	// Lets try to retrieve the Idle IRP from the Idle queue
	//
	status = WdfIoQueueRetrieveNextRequest(
		FxDeviceContext->IdleQueue,
		&request);

	//
	// We did not find the Idle IRP, maybe it was cancelled
	// 
	if (!NT_SUCCESS(status) || (request == NULL))
	{
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Error finding idle notification request in IdleQueue:0x%p - %!STATUS!",
			FxDeviceContext->IdleQueue,
			status);
	}
	else
	{
		//
		// Complete the Idle IRP
		//
		WdfRequestComplete(request, status);

		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"Completed idle notification Request:0x%p from IdleQueue:0x%p - %!STATUS!",
			request,
			FxDeviceContext->IdleQueue,
			status);
	}

	return;
}

VOID
ElanEvtInternalDeviceControl(
IN WDFQUEUE     Queue,
IN WDFREQUEST   Request,
IN size_t       OutputBufferLength,
IN size_t       InputBufferLength,
IN ULONG        IoControlCode
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	WDFDEVICE           device;
	PELAN_CONTEXT     devContext;
	BOOLEAN             completeRequest = TRUE;

	UNREFERENCED_PARAMETER(OutputBufferLength);
	UNREFERENCED_PARAMETER(InputBufferLength);

	device = WdfIoQueueGetDevice(Queue);
	devContext = GetDeviceContext(device);

	ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
		"%s, Queue:0x%p, Request:0x%p\n",
		DbgHidInternalIoctlString(IoControlCode),
		Queue,
		Request
		);

	//
	// Please note that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl. So depending on the ioctl code, we will either
	// use retreive function or escape to WDM to get the UserBuffer.
	//

	switch (IoControlCode)
	{

	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		//
		// Retrieves the device's HID descriptor.
		//
		status = ElanGetHidDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		//
		//Retrieves a device's attributes in a HID_DEVICE_ATTRIBUTES structure.
		//
		status = ElanGetDeviceAttributes(Request);
		break;

	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		//
		//Obtains the report descriptor for the HID device.
		//
		status = ElanGetReportDescriptor(device, Request);
		break;

	case IOCTL_HID_GET_STRING:
		//
		// Requests that the HID minidriver retrieve a human-readable string
		// for either the manufacturer ID, the product ID, or the serial number
		// from the string descriptor of the device. The minidriver must send
		// a Get String Descriptor request to the device, in order to retrieve
		// the string descriptor, then it must extract the string at the
		// appropriate index from the string descriptor and return it in the
		// output buffer indicated by the IRP. Before sending the Get String
		// Descriptor request, the minidriver must retrieve the appropriate
		// index for the manufacturer ID, the product ID or the serial number
		// from the device extension of a top level collection associated with
		// the device.
		//
		status = ElanGetString(Request);
		break;

	case IOCTL_HID_WRITE_REPORT:
	case IOCTL_HID_SET_OUTPUT_REPORT:
		//
		//Transmits a class driver-supplied report to the device.
		//
		status = ElanWriteReport(devContext, Request);
		break;

	case IOCTL_HID_READ_REPORT:
	case IOCTL_HID_GET_INPUT_REPORT:
		//
		// Returns a report from the device into a class driver-supplied buffer.
		// 
		status = ElanReadReport(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SET_FEATURE:
		//
		// This sends a HID class feature report to a top-level collection of
		// a HID class device.
		//
		status = ElanSetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_GET_FEATURE:
		//
		// returns a feature report associated with a top-level collection
		//
		status = ElanGetFeature(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		//Handle HID Idle request
		status = ElanProcessIdleRequest(devContext, Request, &completeRequest);
		break;

	case IOCTL_HID_ACTIVATE_DEVICE:
		//
		// Makes the device ready for I/O operations.
		//
	case IOCTL_HID_DEACTIVATE_DEVICE:
		//
		// Causes the device to cease operations and terminate all outstanding
		// I/O requests.
		//
	default:
		status = STATUS_NOT_SUPPORTED;
		break;
	}

	if (completeRequest)
	{
		WdfRequestComplete(Request, status);

		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s completed, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
			);
	}
	else
	{
		ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
			"%s deferred, Queue:0x%p, Request:0x%p\n",
			DbgHidInternalIoctlString(IoControlCode),
			Queue,
			Request
			);
	}

	return;
}

NTSTATUS
ElanGetHidDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	size_t              bytesToCopy = 0;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetHidDescriptor Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded "HID Descriptor" 
	//
	bytesToCopy = DefaultHidDescriptor.bLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0, // Offset
		(PVOID)&DefaultHidDescriptor,
		bytesToCopy);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetHidDescriptor Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanGetReportDescriptor(
IN WDFDEVICE Device,
IN WDFREQUEST Request
)
{
	NTSTATUS            status = STATUS_SUCCESS;
	ULONG_PTR           bytesToCopy;
	WDFMEMORY           memory;

	UNREFERENCED_PARAMETER(Device);

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetReportDescriptor Entry\n");

	PELAN_CONTEXT devContext = GetDeviceContext(Device);

#define MT_TOUCH_COLLECTION												\
			MT_TOUCH_COLLECTION0 \
			0x26, devContext->max_x_hid[0], devContext->max_x_hid[1],                   /*       LOGICAL_MAXIMUM (WIDTH)    */ \
			0x46, devContext->phy_x_hid[0], devContext->phy_x_hid[1],                   /*       PHYSICAL_MAXIMUM (WIDTH)   */ \
			MT_TOUCH_COLLECTION1 \
			0x26, devContext->max_y_hid[0], devContext->max_y_hid[1],                   /*       LOGICAL_MAXIMUM (HEIGHT)    */ \
			0x46, devContext->phy_y_hid[0], devContext->phy_y_hid[1],                   /*       PHYSICAL_MAXIMUM (HEIGHT)   */ \
			MT_TOUCH_COLLECTION2

	HID_REPORT_DESCRIPTOR ReportDescriptor[] = {
		//
		// Multitouch report starts here
		//
		//TOUCH PAD input TLC
		0x05, 0x0d,                         // USAGE_PAGE (Digitizers)          
		0x09, 0x05,                         // USAGE (Touch Pad)             
		0xa1, 0x01,                         // COLLECTION (Application)         
		0x85, REPORTID_MTOUCH,            //   REPORT_ID (Touch pad)              
		0x09, 0x22,                         //   USAGE (Finger)                 
		MT_TOUCH_COLLECTION
		MT_TOUCH_COLLECTION
		MT_TOUCH_COLLECTION
		MT_TOUCH_COLLECTION
		MT_TOUCH_COLLECTION
		USAGE_PAGES
	};

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputMemory(Request, &memory);
	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputMemory failed 0x%x\n", status);

		return status;
	}

	//
	// Use hardcoded Report descriptor
	//
	bytesToCopy = DefaultHidDescriptor.DescriptorList[0].wReportLength;

	if (bytesToCopy == 0)
	{
		status = STATUS_INVALID_DEVICE_STATE;

		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"DefaultHidDescriptor's reportLength is zero, 0x%x\n", status);

		return status;
	}

	status = WdfMemoryCopyFromBuffer(memory,
		0,
		(PVOID)ReportDescriptor,
		bytesToCopy);
	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfMemoryCopyFromBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, bytesToCopy);

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetReportDescriptor Exit = 0x%x\n", status);

	return status;
}


NTSTATUS
ElanGetDeviceAttributes(
IN WDFREQUEST Request
)
{
	NTSTATUS                 status = STATUS_SUCCESS;
	PHID_DEVICE_ATTRIBUTES   deviceAttributes = NULL;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetDeviceAttributes Entry\n");

	//
	// This IOCTL is METHOD_NEITHER so WdfRequestRetrieveOutputMemory
	// will correctly retrieve buffer from Irp->UserBuffer. 
	// Remember that HIDCLASS provides the buffer in the Irp->UserBuffer
	// field irrespective of the ioctl buffer type. However, framework is very
	// strict about type checking. You cannot get Irp->UserBuffer by using
	// WdfRequestRetrieveOutputMemory if the ioctl is not a METHOD_NEITHER
	// internal ioctl.
	//
	status = WdfRequestRetrieveOutputBuffer(Request,
		sizeof(HID_DEVICE_ATTRIBUTES),
		&deviceAttributes,
		NULL);
	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestRetrieveOutputBuffer failed 0x%x\n", status);

		return status;
	}

	//
	// Set USB device descriptor
	//

	deviceAttributes->Size = sizeof(HID_DEVICE_ATTRIBUTES);
	deviceAttributes->VendorID = ELAN_VID;
	deviceAttributes->ProductID = ELAN_PID;
	deviceAttributes->VersionNumber = ELAN_VERSION;

	//
	// Report how many bytes were copied
	//
	WdfRequestSetInformation(Request, sizeof(HID_DEVICE_ATTRIBUTES));

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetDeviceAttributes Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanGetString(
IN WDFREQUEST Request
)
{

	NTSTATUS status = STATUS_SUCCESS;
	PWSTR pwstrID;
	size_t lenID;
	WDF_REQUEST_PARAMETERS params;
	void *pStringBuffer = NULL;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetString Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	switch ((ULONG_PTR)params.Parameters.DeviceIoControl.Type3InputBuffer & 0xFFFF)
	{
	case HID_STRING_ID_IMANUFACTURER:
		pwstrID = L"Elan.\0";
		break;

	case HID_STRING_ID_IPRODUCT:
		pwstrID = L"MaxTouch Touch Screen\0";
		break;

	case HID_STRING_ID_ISERIALNUMBER:
		pwstrID = L"123123123\0";
		break;

	default:
		pwstrID = NULL;
		break;
	}

	lenID = pwstrID ? wcslen(pwstrID)*sizeof(WCHAR) + sizeof(UNICODE_NULL) : 0;

	if (pwstrID == NULL)
	{

		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"ElanGetString Invalid request type\n");

		status = STATUS_INVALID_PARAMETER;

		return status;
	}

	status = WdfRequestRetrieveOutputBuffer(Request,
		lenID,
		&pStringBuffer,
		&lenID);

	if (!NT_SUCCESS(status))
	{

		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"ElanGetString WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);

		return status;
	}

	RtlCopyMemory(pStringBuffer, pwstrID, lenID);

	WdfRequestSetInformation(Request, lenID);

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetString Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanWriteReport(
IN PELAN_CONTEXT DevContext,
IN WDFREQUEST Request
)
{
	UNREFERENCED_PARAMETER(DevContext);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanWriteReport Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"ElanWriteReport Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"ElanWriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			default:

				ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"ElanWriteReport Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanWriteReport Exit = 0x%x\n", status);

	return status;

}

NTSTATUS
ElanProcessVendorReport(
IN PELAN_CONTEXT DevContext,
IN PVOID ReportBuffer,
IN ULONG ReportBufferLen,
OUT size_t* BytesWritten
)
{
	NTSTATUS status = STATUS_SUCCESS;
	WDFREQUEST reqRead;
	PVOID pReadReport = NULL;
	size_t bytesReturned = 0;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanProcessVendorReport Entry\n");

	status = WdfIoQueueRetrieveNextRequest(DevContext->ReportQueue,
		&reqRead);

	if (NT_SUCCESS(status))
	{
		status = WdfRequestRetrieveOutputBuffer(reqRead,
			ReportBufferLen,
			&pReadReport,
			&bytesReturned);

		if (NT_SUCCESS(status))
		{
			//
			// Copy ReportBuffer into read request
			//

			if (bytesReturned > ReportBufferLen)
			{
				bytesReturned = ReportBufferLen;
			}

			RtlCopyMemory(pReadReport,
				ReportBuffer,
				bytesReturned);

			//
			// Complete read with the number of bytes returned as info
			//

			WdfRequestCompleteWithInformation(reqRead,
				status,
				bytesReturned);

			ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"ElanProcessVendorReport %d bytes returned\n", bytesReturned);

			//
			// Return the number of bytes written for the write request completion
			//

			*BytesWritten = bytesReturned;

			ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
				"%s completed, Queue:0x%p, Request:0x%p\n",
				DbgHidInternalIoctlString(IOCTL_HID_READ_REPORT),
				DevContext->ReportQueue,
				reqRead);
		}
		else
		{
			ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"WdfRequestRetrieveOutputBuffer failed Status 0x%x\n", status);
		}
	}
	else
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfIoQueueRetrieveNextRequest failed Status 0x%x\n", status);
	}

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanProcessVendorReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanReadReport(
IN PELAN_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	NTSTATUS status = STATUS_SUCCESS;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanReadReport Entry\n");

	//
	// Forward this read request to our manual queue
	// (in other words, we are going to defer this request
	// until we have a corresponding write request to
	// match it with)
	//

	status = WdfRequestForwardToIoQueue(Request, DevContext->ReportQueue);

	if (!NT_SUCCESS(status))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"WdfRequestForwardToIoQueue failed Status 0x%x\n", status);
	}
	else
	{
		*CompleteRequest = FALSE;
	}

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanReadReport Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanSetFeature(
IN PELAN_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	UNREFERENCED_PARAMETER(CompleteRequest);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;
	ElanFeatureReport* pReport = NULL;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanSetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.InputBufferLength < sizeof(HID_XFER_PACKET))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"ElanSetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"ElanWriteReport No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_FEATURE:

				if (transferPacket->reportBufferLen == sizeof(ElanFeatureReport))
				{
					pReport = (ElanFeatureReport*)transferPacket->reportBuffer;

					DevContext->DeviceMode = pReport->DeviceMode;

					ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"ElanSetFeature DeviceMode = 0x%x\n", DevContext->DeviceMode);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"ElanSetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(ElanFeatureReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(ElanFeatureReport));
				}

				break;

			default:

				ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"ElanSetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanSetFeature Exit = 0x%x\n", status);

	return status;
}

NTSTATUS
ElanGetFeature(
IN PELAN_CONTEXT DevContext,
IN WDFREQUEST Request,
OUT BOOLEAN* CompleteRequest
)
{
	UNREFERENCED_PARAMETER(CompleteRequest);

	NTSTATUS status = STATUS_SUCCESS;
	WDF_REQUEST_PARAMETERS params;
	PHID_XFER_PACKET transferPacket = NULL;

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetFeature Entry\n");

	WDF_REQUEST_PARAMETERS_INIT(&params);
	WdfRequestGetParameters(Request, &params);

	if (params.Parameters.DeviceIoControl.OutputBufferLength < sizeof(HID_XFER_PACKET))
	{
		ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
			"ElanGetFeature Xfer packet too small\n");

		status = STATUS_BUFFER_TOO_SMALL;
	}
	else
	{

		transferPacket = (PHID_XFER_PACKET)WdfRequestWdmGetIrp(Request)->UserBuffer;

		if (transferPacket == NULL)
		{
			ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
				"ElanGetFeature No xfer packet\n");

			status = STATUS_INVALID_DEVICE_REQUEST;
		}
		else
		{
			//
			// switch on the report id
			//

			switch (transferPacket->reportId)
			{
			case REPORTID_MTOUCH:
			{

				ElanMaxCountReport* pReport = NULL;
				ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
					"ElanGetFeature MaximumCount = 0x%x\n", MULTI_MAX_COUNT);
				if (transferPacket->reportBufferLen >= sizeof(ElanMaxCountReport))
				{
					pReport = (ElanMaxCountReport*)transferPacket->reportBuffer;

					pReport->MaximumCount = MULTI_MAX_COUNT;

					pReport->PadType = 0;

					ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"ElanGetFeature MaximumCount = 0x%x\n", MULTI_MAX_COUNT);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"ElanGetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(ElanMaxCountReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(ElanMaxCountReport));
				}

				break;
			}

			case REPORTID_FEATURE:
			{

				ElanFeatureReport* pReport = NULL;

				if (transferPacket->reportBufferLen >= sizeof(ElanFeatureReport))
				{
					pReport = (ElanFeatureReport*)transferPacket->reportBuffer;

					pReport->DeviceMode = DevContext->DeviceMode;

					pReport->DeviceIdentifier = 0;

					ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"ElanGetFeature DeviceMode = 0x%x\n", DevContext->DeviceMode);
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"ElanGetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(ElanFeatureReport) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(ElanFeatureReport));
				}

				break;
			}

			case REPORTID_PTPHQA:
			{
				uint8_t PTPHQA_BLOB[] = { REPORTID_PTPHQA, 0xfc, 0x28, 0xfe, 0x84, 0x40, 0xcb, 0x9a, 0x87, 0x0d, 0xbe, 0x57, 0x3c, 0xb6, 0x70, 0x09, 0x88, 0x07,\
					0x97, 0x2d, 0x2b, 0xe3, 0x38, 0x34, 0xb6, 0x6c, 0xed, 0xb0, 0xf7, 0xe5, 0x9c, 0xf6, 0xc2, 0x2e, 0x84,\
					0x1b, 0xe8, 0xb4, 0x51, 0x78, 0x43, 0x1f, 0x28, 0x4b, 0x7c, 0x2d, 0x53, 0xaf, 0xfc, 0x47, 0x70, 0x1b,\
					0x59, 0x6f, 0x74, 0x43, 0xc4, 0xf3, 0x47, 0x18, 0x53, 0x1a, 0xa2, 0xa1, 0x71, 0xc7, 0x95, 0x0e, 0x31,\
					0x55, 0x21, 0xd3, 0xb5, 0x1e, 0xe9, 0x0c, 0xba, 0xec, 0xb8, 0x89, 0x19, 0x3e, 0xb3, 0xaf, 0x75, 0x81,\
					0x9d, 0x53, 0xb9, 0x41, 0x57, 0xf4, 0x6d, 0x39, 0x25, 0x29, 0x7c, 0x87, 0xd9, 0xb4, 0x98, 0x45, 0x7d,\
					0xa7, 0x26, 0x9c, 0x65, 0x3b, 0x85, 0x68, 0x89, 0xd7, 0x3b, 0xbd, 0xff, 0x14, 0x67, 0xf2, 0x2b, 0xf0,\
					0x2a, 0x41, 0x54, 0xf0, 0xfd, 0x2c, 0x66, 0x7c, 0xf8, 0xc0, 0x8f, 0x33, 0x13, 0x03, 0xf1, 0xd3, 0xc1, 0x0b,\
					0x89, 0xd9, 0x1b, 0x62, 0xcd, 0x51, 0xb7, 0x80, 0xb8, 0xaf, 0x3a, 0x10, 0xc1, 0x8a, 0x5b, 0xe8, 0x8a,\
					0x56, 0xf0, 0x8c, 0xaa, 0xfa, 0x35, 0xe9, 0x42, 0xc4, 0xd8, 0x55, 0xc3, 0x38, 0xcc, 0x2b, 0x53, 0x5c,\
					0x69, 0x52, 0xd5, 0xc8, 0x73, 0x02, 0x38, 0x7c, 0x73, 0xb6, 0x41, 0xe7, 0xff, 0x05, 0xd8, 0x2b, 0x79,\
					0x9a, 0xe2, 0x34, 0x60, 0x8f, 0xa3, 0x32, 0x1f, 0x09, 0x78, 0x62, 0xbc, 0x80, 0xe3, 0x0f, 0xbd, 0x65,\
					0x20, 0x08, 0x13, 0xc1, 0xe2, 0xee, 0x53, 0x2d, 0x86, 0x7e, 0xa7, 0x5a, 0xc5, 0xd3, 0x7d, 0x98, 0xbe,\
					0x31, 0x48, 0x1f, 0xfb, 0xda, 0xaf, 0xa2, 0xa8, 0x6a, 0x89, 0xd6, 0xbf, 0xf2, 0xd3, 0x32, 0x2a, 0x9a,\
					0xe4, 0xcf, 0x17, 0xb7, 0xb8, 0xf4, 0xe1, 0x33, 0x08, 0x24, 0x8b, 0xc4, 0x43, 0xa5, 0xe5, 0x24, 0xc2 };
				if (transferPacket->reportBufferLen >= sizeof(PTPHQA_BLOB))
				{
					uint8_t *blobBuffer = (uint8_t*)transferPacket->reportBuffer;
					for (int i = 0; i < sizeof(PTPHQA_BLOB); i++) {
						blobBuffer[i] = PTPHQA_BLOB[i];
					}
					ElanPrint(DEBUG_LEVEL_INFO, DBG_IOCTL,
						"ElanGetFeature PHPHQA\n");
				}
				else
				{
					status = STATUS_INVALID_PARAMETER;

					ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
						"ElanGetFeature Error transferPacket->reportBufferLen (%d) is different from sizeof(PTPHEQ_BLOB) (%d)\n",
						transferPacket->reportBufferLen,
						sizeof(ElanFeatureReport));
				}
				break;
			}

			default:

				ElanPrint(DEBUG_LEVEL_ERROR, DBG_IOCTL,
					"ElanGetFeature Unhandled report type %d\n", transferPacket->reportId);

				status = STATUS_INVALID_PARAMETER;

				break;
			}
		}
	}

	ElanPrint(DEBUG_LEVEL_VERBOSE, DBG_IOCTL,
		"ElanGetFeature Exit = 0x%x\n", status);

	return status;
}

PCHAR
DbgHidInternalIoctlString(
IN ULONG IoControlCode
)
{
	switch (IoControlCode)
	{
	case IOCTL_HID_GET_DEVICE_DESCRIPTOR:
		return "IOCTL_HID_GET_DEVICE_DESCRIPTOR";
	case IOCTL_HID_GET_REPORT_DESCRIPTOR:
		return "IOCTL_HID_GET_REPORT_DESCRIPTOR";
	case IOCTL_HID_READ_REPORT:
		return "IOCTL_HID_READ_REPORT";
	case IOCTL_HID_GET_DEVICE_ATTRIBUTES:
		return "IOCTL_HID_GET_DEVICE_ATTRIBUTES";
	case IOCTL_HID_WRITE_REPORT:
		return "IOCTL_HID_WRITE_REPORT";
	case IOCTL_HID_SET_FEATURE:
		return "IOCTL_HID_SET_FEATURE";
	case IOCTL_HID_GET_FEATURE:
		return "IOCTL_HID_GET_FEATURE";
	case IOCTL_HID_GET_STRING:
		return "IOCTL_HID_GET_STRING";
	case IOCTL_HID_ACTIVATE_DEVICE:
		return "IOCTL_HID_ACTIVATE_DEVICE";
	case IOCTL_HID_DEACTIVATE_DEVICE:
		return "IOCTL_HID_DEACTIVATE_DEVICE";
	case IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST:
		return "IOCTL_HID_SEND_IDLE_NOTIFICATION_REQUEST";
	case IOCTL_HID_SET_OUTPUT_REPORT:
		return "IOCTL_HID_SET_OUTPUT_REPORT";
	case IOCTL_HID_GET_INPUT_REPORT:
		return "IOCTL_HID_GET_INPUT_REPORT";
	default:
		return "Unknown IOCTL";
	}
}
