/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_RTS54XX_DEVICE_H
#define __FU_RTS54XX_DEVICE_H

#include <glib-object.h>
#include <gusb.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_RTS54XX_DEVICE (fu_rts54xx_device_get_type ())
G_DECLARE_FINAL_TYPE (FuRts54xxDevice, fu_rts54xx_device, FU, RTS54XX_DEVICE, FuUsbDevice)

FuRts54xxDevice *fu_rts54xx_device_new		(GUsbDevice	*usb_device);

G_END_DECLS

#endif /* __FU_RTS54XX_DEVICE_H */
