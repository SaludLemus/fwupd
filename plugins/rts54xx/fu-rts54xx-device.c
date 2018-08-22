/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#include "config.h"

#include <string.h>

#include "fu-chunk.h"
#include "fu-rts54xx-device.h"

struct _FuRts54xxDevice {
	FuUsbDevice			 parent_instance;
	gboolean			 fw_auth;
	gboolean			 dual_bank;
};

G_DEFINE_TYPE (FuRts54xxDevice, fu_rts54xx_device, FU_TYPE_USB_DEVICE)

#define FU_RTS54XX_DEVICE_TRANSFER_BLOCK_SIZE		0x80
#define FU_RTS54XX_DEVICE_TIMEOUT			1000 /* ms */

#define FU_RTS54XX_DEVICE_REPORT_LENGTH			0xc0
#define FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET		0x00
#define FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET		0x40

#define FU_RTS54XX_DEVICE_I2C_SPEED_250K		0x00
#define FU_RTS54XX_DEVICE_I2C_SPEED_400K		0x01
#define FU_RTS54XX_DEVICE_I2C_SPEED_800K		0x02

#define FU_RTS54XX_DEVICE_REQUEST_KIND_QUERY		0xc0
#define FU_RTS54XX_DEVICE_REQUEST_KIND_CMD		0x40

#define FU_RTS54XX_DEVICE_REQUEST_SET_CLOCK_MODE	0x06
#define FU_RTS54XX_DEVICE_REQUEST_READ_STATUS		0x09
#define FU_RTS54XX_DEVICE_REQUEST_WRITE_FLASH		0xc8
#define FU_RTS54XX_DEVICE_REQUEST_FW_AUTHENTICATION	0xd9
#define FU_RTS54XX_DEVICE_REQUEST_I2C_WRITE		0xc6
#define FU_RTS54XX_DEVICE_REQUEST_I2C_READ		0xd6
#define FU_RTS54XX_DEVICE_REQUEST_BANK_ERASE		0xe8
#define FU_RTS54XX_DEVICE_REQUEST_RESET_TO_FLASH	0xe9

static void
fu_rts54xx_device_to_string (FuDevice *device, GString *str)
{
	FuRts54xxDevice *self = FU_RTS54XX_DEVICE (device);
	g_string_append (str, "  FuRts54xxDevice:\n");
	g_string_append_printf (str, "    fw-auth: %i\n", self->fw_auth);
	g_string_append_printf (str, "    dual-bank: %i\n", self->dual_bank);
}

static gboolean
fu_rts54xx_device_set_report (FuRts54xxDevice *self,
			      guint8 *buf, gsize buf_sz,
			      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    HID_REPORT_SET,
					    0x0200, 0x0000,
					    buf, buf_sz,
					    NULL, /* actual length */
					    FU_RTS54XX_DEVICE_TIMEOUT * 2,
					    NULL, error)) {
		g_prefix_error (error, "failed to SetReport: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_get_report (FuRts54xxDevice *self,
			      guint8 *buf, gsize buf_sz,
			      GError **error)
{
	GUsbDevice *usb_device = fu_usb_device_get_dev (FU_USB_DEVICE (self));
	if (!g_usb_device_control_transfer (usb_device,
					    G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
					    G_USB_DEVICE_REQUEST_TYPE_CLASS,
					    G_USB_DEVICE_RECIPIENT_INTERFACE,
					    HID_REPORT_GET,
					    0x0100, 0x0000,
					    buf, buf_sz,
					    NULL, /* actual length */
					    FU_RTS54XX_DEVICE_TIMEOUT,
					    NULL, error)) {
		g_prefix_error (error, "failed to GetReport: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_send_in (FuRts54xxDevice *self,
			   const guint8 *cmdbuf, gsize cmdbuf_sz,
			   const guint8 *databuf, gsize databuf_sz,
			   guint8 *outbuf, gsize outbuf_sz,
			   guint delay_secs, GError **error)
{
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };

	g_return_val_if_fail (cmdbuf != NULL, FALSE);
	g_return_val_if_fail (cmdbuf_sz != 0, FALSE);

	/* create request */
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmdbuf, cmdbuf_sz);
	if (databuf != NULL)
		memcpy (buf + FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET, databuf, databuf_sz);

	/* set then get */
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error))
		return FALSE;
	g_usleep (delay_secs * G_USEC_PER_SEC);
	if (!fu_rts54xx_device_get_report (self, buf, sizeof(buf), error))
		return FALSE;

	/* copy out data */
	if (outbuf != NULL)
		memcpy (outbuf, buf, outbuf_sz);
	return TRUE;
}

static gboolean
fu_rts54xx_device_set_clock_mode (FuRts54xxDevice *self, gboolean val, GError **error)
{
	const guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_CMD,
			       FU_RTS54XX_DEVICE_REQUEST_SET_CLOCK_MODE,
			       val, 0x00, 0x00, 0x00, 0x00, 0x00 };
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to set clock-mode=%i: ", val);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_reset_to_flash (FuRts54xxDevice *self, GError **error)
{
	const guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_CMD,
			       FU_RTS54XX_DEVICE_REQUEST_RESET_TO_FLASH,
			       0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to soft reset: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_write_flash (FuRts54xxDevice *self,
			       guint32 addr,
			       const guint8 *data,
			       guint8 data_sz,
			       GError **error)
{
	guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_CMD,
			 FU_RTS54XX_DEVICE_REQUEST_WRITE_FLASH,
			 0xff, 0xff, 0xff, 0xff,
			 data_sz, 0x00 };
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };

	g_return_val_if_fail (data_sz <= 128, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data_sz != 0, FALSE);

	fu_common_write_uint32 (cmd + 2, addr, G_LITTLE_ENDIAN);
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET, data, data_sz);
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to write flash @%08x: ", (guint) addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_verify_update_fw (FuRts54xxDevice *self, GError **error)
{
	const guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_CMD,
			       FU_RTS54XX_DEVICE_REQUEST_FW_AUTHENTICATION,
			       0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
	guint8 outbuf[1];

	/* read command */
	if (!fu_rts54xx_device_send_in (self,
					cmd, sizeof(cmd),
					NULL, 0, /* data */
					outbuf, sizeof(outbuf),
					4, error))
		return FALSE;

	/* check device status */
	if (outbuf[0] != 0x01) {
		g_set_error_literal (error,
				     FWUPD_ERROR,
				     FWUPD_ERROR_WRITE,
				     "firmware flash failed");
		return FALSE;
	}

	/* success */
	return TRUE;
}

static gboolean
fu_rts54xx_device_erase_spare_bank (FuRts54xxDevice *self, GError **error)
{
	const guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_CMD,
			       FU_RTS54XX_DEVICE_REQUEST_BANK_ERASE,
			       0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to erase spare bank: ");
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_ensure_status (FuRts54xxDevice *self, GError **error)
{
	guint8 cmd[] = { FU_RTS54XX_DEVICE_REQUEST_KIND_QUERY,
			 FU_RTS54XX_DEVICE_REQUEST_READ_STATUS,
			 0x00, 0x00, 0x00, 0x00, 0xff, 0xff };
	guint8 outbuf[32];

	/* read command */
	fu_common_write_uint32 (cmd + 6, sizeof(outbuf), G_LITTLE_ENDIAN);
	if (!fu_rts54xx_device_send_in (self,
					cmd, sizeof(cmd),
					NULL, 0, /* data */
					outbuf, sizeof(outbuf),
					0, error))
		return FALSE;

	/* check the hardware capabilities */
	self->dual_bank = (outbuf[7] & 0xf0) == 0x80;
	self->fw_auth = (outbuf[13] & 0x02) > 0;
	return TRUE;
}

static gboolean
fu_rts54xx_device_open (FuUsbDevice *device, GError **error)
{
	FuRts54xxDevice *self = FU_RTS54XX_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* disconnect, set config, reattach kernel driver */
	if (!g_usb_device_set_configuration (usb_device, 0x00, error))
		return FALSE;
	if (!g_usb_device_claim_interface (usb_device, 0x00, /* HID */
					   G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					   error)) {
		g_prefix_error (error, "failed to claim interface: ");
		return FALSE;
	}

	/* check this device is correct */
	if (!fu_rts54xx_device_ensure_status (self, error))
		return FALSE;

	/* we know this is supported now */
	if (self->dual_bank && self->fw_auth)
		fu_device_add_flag (device, FWUPD_DEVICE_FLAG_UPDATABLE);

	/* success */
	return TRUE;
}

static gboolean
fu_rts54xx_device_close (FuUsbDevice *device, GError **error)
{
	FuRts54xxDevice *self = FU_RTS54XX_DEVICE (device);
	GUsbDevice *usb_device = fu_usb_device_get_dev (device);

	/* set MCU to normal clock rate */
	if (!fu_rts54xx_device_set_clock_mode (self, FALSE, error))
		return FALSE;

	/* we're done here */
	if (!g_usb_device_release_interface (usb_device, 0x00, /* HID */
					     G_USB_DEVICE_CLAIM_INTERFACE_BIND_KERNEL_DRIVER,
					     error)) {
		g_prefix_error (error, "failed to release interface: ");
		return FALSE;
	}

	/* success */
	return TRUE;
}

#if 0

static gboolean
fu_rts54xx_device_i2c_write (FuRts54xxDevice *self,
			     const guint8 *data,
			     guint8 data_sz,
			     guint8 slave_addr,
			     guint8 i2c_speed,
			     GError **error)
{
	const guint8 cmd[] = {FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET,
			      FU_RTS54XX_DEVICE_REQUEST_I2C_WRITE,
			      0x00, 0x00, 0x00, 0x00, data_sz,
			      0x00, slave_addr, 0x00, i2c_speed | 0x80};
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };

	g_return_val_if_fail (data_sz <= 128, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data_sz != 0, FALSE);

	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET, data, data_sz);
	if (!fu_rts54xx_device_set_report (self, buf, sizeof(buf), error)) {
		g_prefix_error (error, "failed to write i2c @%08x: ", (guint) slave_addr);
		return FALSE;
	}
	return TRUE;
}

static gboolean
fu_rts54xx_device_i2c_read (FuRts54xxDevice *self,
			    guint32 addr,
			    guint8 *data,
			    guint8 data_sz,
			    guint8 slave_addr,
			    guint8 i2c_speed,
			    GError **error)
{
	const guint8 cmd[] = {FU_RTS54XX_DEVICE_REPORT_DATA_OFFSET,
			      FU_RTS54XX_DEVICE_REQUEST_I2C_READ,
			      0xff, 0xff, 0xff, 0xff, data_sz,
			      0x00, slave_addr, 0x04, i2c_speed | 0x80};
	guint8 buf[FU_RTS54XX_DEVICE_REPORT_LENGTH] = { 0 };

	g_return_val_if_fail (data_sz <= 192, FALSE);
	g_return_val_if_fail (data != NULL, FALSE);
	g_return_val_if_fail (data_sz != 0, FALSE);

	/* read from device */
	fu_common_write_uint32 (cmd + 2, addr, G_LITTLE_ENDIAN);
	memcpy (buf + FU_RTS54XX_DEVICE_REPORT_CMD_OFFSET, cmd, sizeof(cmd));
	if (!fu_rts54xx_device_send_in (self,
					cmd, sizeof(cmd),
					NULL, 0, /* databuf */
					data, data_sz,
					0, /* delay_secs */
					error)) {
		g_prefix_error (error, "failed to write i2c @%08x: ", (guint) slave_addr);
		return FALSE;
	}
	return TRUE;
}
#endif

static gboolean
fu_rts54xx_device_write_firmware (FuDevice *device, GBytes *fw, GError **error)
{
	FuRts54xxDevice *self = FU_RTS54XX_DEVICE (device);
	g_autoptr(GPtrArray) chunks = NULL;

	/* set MCU to high clock rate for better ISP performance */
	if (!fu_rts54xx_device_set_clock_mode (self, TRUE, error))
		return FALSE;

	/* erase spare flash bank only if it is not empty */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_ERASE);
	if (!fu_rts54xx_device_erase_spare_bank (self, error))
		return FALSE;

	/* build packets */
	chunks = fu_chunk_array_new_from_bytes (fw,
						0x00,	/* start addr */
						0x00,	/* page_sz */
						FU_RTS54XX_DEVICE_TRANSFER_BLOCK_SIZE);

	/* write each block */
	fu_device_set_status (device, FWUPD_STATUS_DEVICE_WRITE);
	for (guint i = 0; i < chunks->len; i++) {
		FuChunk *chk = g_ptr_array_index (chunks, i);

		/* write chunk */
		if (!fu_rts54xx_device_write_flash (self,
						    chk->address,
						    chk->data,
						    chk->data_sz,
						    error))
			return FALSE;

		/* update progress */
		fu_device_set_progress_full (device, (gsize) i, (gsize) chunks->len * 2);
	}

	/* get device to authenticate the firmware */
	if (!fu_rts54xx_device_verify_update_fw (self, error))
		return FALSE;

	/* send software reset to run available flash code */
	if (!fu_rts54xx_device_reset_to_flash (self, error))
		return FALSE;

	/* success! */
	return TRUE;
}

static void
fu_rts54xx_device_init (FuRts54xxDevice *self)
{
}

static void
fu_rts54xx_device_class_init (FuRts54xxDeviceClass *klass)
{
	FuDeviceClass *klass_device = FU_DEVICE_CLASS (klass);
	FuUsbDeviceClass *klass_usb_device = FU_USB_DEVICE_CLASS (klass);
	klass_device->write_firmware = fu_rts54xx_device_write_firmware;
	klass_device->to_string = fu_rts54xx_device_to_string;
	klass_usb_device->open = fu_rts54xx_device_open;
	klass_usb_device->close = fu_rts54xx_device_close;
}

FuRts54xxDevice *
fu_rts54xx_device_new (GUsbDevice *usb_device)
{
	FuRts54xxDevice *self = NULL;
	self = g_object_new (FU_TYPE_RTS54XX_DEVICE,
			     "usb-device", usb_device,
			     NULL);
	return self;
}
