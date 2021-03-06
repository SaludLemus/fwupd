/*
 * Copyright (C) 2018 Richard Hughes <richard@hughsie.com>
 *
 * SPDX-License-Identifier: LGPL-2.1+
 */

#ifndef __FU_NVME_DEVICE_H
#define __FU_NVME_DEVICE_H

#include <glib-object.h>
#include <gudev/gudev.h>

#include "fu-plugin.h"

G_BEGIN_DECLS

#define FU_TYPE_NVME_DEVICE (fu_nvme_device_get_type ())
G_DECLARE_FINAL_TYPE (FuNvmeDevice, fu_nvme_device, FU, NVME_DEVICE, FuUdevDevice)

FuNvmeDevice	*fu_nvme_device_new			(GUdevDevice	*udev_device);
FuNvmeDevice	*fu_nvme_device_new_from_blob		(const guint8	*buf,
							 gsize		 sz,
							 GError		**error);

G_END_DECLS

#endif /* __FU_NVME_DEVICE_H */
