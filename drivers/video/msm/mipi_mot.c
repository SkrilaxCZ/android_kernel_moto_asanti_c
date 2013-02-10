/* Copyright (c) 2010-2011, Code Aurora Forum. All rights reserved.
 * Copyright (c) 2011, Motorola Mobility, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include "msm_fb.h"
#include "mipi_dsi.h"
#include "mipi_mot.h"
#include "mdp4.h"
#include <linux/atomic.h>
#include <linux/reboot.h>

static struct mipi_dsi_panel_platform_data *mipi_mot_pdata;

static struct mipi_mot_panel mot_panel;
static bool factory_run;

static struct dsi_buf mot_tx_buf;
static struct dsi_buf mot_rx_buf;

unsigned short display_hw_rev_txt_manufacturer;
unsigned short display_hw_rev_txt_controller;
unsigned short display_hw_rev_txt_controller_drv;

static int get_manufacture_id(struct msm_fb_data_type *mfd)
{
	static int manufacture_id = INVALID_VALUE;
	display_hw_rev_txt_manufacturer = 0;

	if (manufacture_id == INVALID_VALUE) {
		if (mot_panel.get_manufacture_id) {
			mipi_dsi_cmd_bta_sw_trigger();
			manufacture_id = mot_panel.get_manufacture_id(mfd);
		} else {
			pr_err("%s: can not locate get_manufacture_id()\n",
								__func__);
			goto end;
		}

		if (manufacture_id == -1)
			pr_err("%s: failed to retrieve manufacture_id\n",
								__func__);
		else {
			pr_info(" MIPI panel Manufacture_id = 0x%x\n",
							manufacture_id);
			display_hw_rev_txt_manufacturer = manufacture_id;
		}
	}

end:
	return manufacture_id;
}

static int get_controller_ver(struct msm_fb_data_type *mfd)
{
	static int controller_ver = INVALID_VALUE;
	display_hw_rev_txt_controller = 0;

	if (controller_ver == INVALID_VALUE) {
		if (mot_panel.get_controller_ver)
			controller_ver = mot_panel.get_controller_ver(mfd);
		else {
			pr_err("%s: can not locate get_controller_ver()\n",
								__func__);
			goto end;
		}

		if (controller_ver == -1)
			pr_err("%s: failed to retrieve controller_ver\n",
								__func__);
		else {
			pr_info(" MIPI panel Controller_ver = 0x%x\n",
								controller_ver);
			display_hw_rev_txt_controller = controller_ver;
		}
	}
end:
	return controller_ver;
}


static int get_controller_drv_ver(struct msm_fb_data_type *mfd)
{
	static int controller_drv_ver = INVALID_VALUE;
	display_hw_rev_txt_controller_drv = 0;

	if (controller_drv_ver == INVALID_VALUE) {
		if (mot_panel.get_controller_drv_ver)
			controller_drv_ver =
				mot_panel.get_controller_drv_ver(mfd);
		else {
			pr_err("%s: cannot locate get_controller_drv_ver()\n",
								__func__);
			goto end;
		}

		if (controller_drv_ver == -1)
			pr_err("%s: failed to retrieve controller_drv_ver\n",
								__func__);
		else {
			pr_info(" MIPI panel Controller_drv_ver = 0x%x\n",
							controller_drv_ver);
			display_hw_rev_txt_controller_drv = controller_drv_ver;
		}
	}

end:
        return controller_drv_ver;
}

static ssize_t panel_acl_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 data = 0xff;

	if (mot_panel.acl_support_present == FALSE) {
		pr_err("%s: panel doesn't support ACL\n", __func__);
		data = -EPERM;
		goto err;
	}

	mutex_lock(&mot_panel.lock);
	if (mot_panel.acl_enabled)
		data = 1;
	else
		data = 0;
	mutex_unlock(&mot_panel.lock);
err:
	return snprintf(buf, PAGE_SIZE, "%d\n", ((u32) data));
}

static ssize_t panel_acl_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long acl_val = 0;
	unsigned long r = 0;

	if (mot_panel.mfd == 0) {
		r = -ENODEV;
		goto end;
	}
	if (mot_panel.acl_support_present == TRUE) {
		r = strict_strtoul(buf, 0, &acl_val);
		if ((r) || ((acl_val != 0) && (acl_val != 1))) {
			pr_err("%s: Invalid ACL value = %lu\n",
							__func__, acl_val);
			r = -EINVAL;
			goto end;
		}
		mutex_lock(&mot_panel.lock);
		if (mot_panel.acl_enabled != acl_val) {
			mot_panel.acl_enabled = acl_val;
			mot_panel.enable_acl(mot_panel.mfd);
		}
		mutex_unlock(&mot_panel.lock);
	}

end:
	return r ? r : count;
}
static DEVICE_ATTR(acl_mode, S_IRUGO | S_IWGRP,
					panel_acl_show, panel_acl_store);
static struct attribute *acl_attrs[] = {
	&dev_attr_acl_mode.attr,
	NULL,
};
static struct attribute_group acl_attr_group = {
	.attrs = acl_attrs,
};

static ssize_t elvss_tth_status_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	u8 data = 0xff;

	if (mot_panel.elvss_tth_support_present == FALSE) {
		pr_err("%s: panel doesn't support adjust elvss based on temp\n",
			 __func__);
		data = -EPERM;
		goto err;
	}

	mutex_lock(&mot_panel.lock);
	if (mot_panel.elvss_tth_status)
		data = 1;
	else
		data = 0;
	mutex_unlock(&mot_panel.lock);
err:
	return snprintf(buf, PAGE_SIZE, "%d\n", ((u32) data));
}

static ssize_t elvss_tth_status_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long elvss_tth_val = 0;
	unsigned long r = 0;

	if (mot_panel.mfd == 0) {
		r = -ENODEV;
		goto end;
	}
	if (mot_panel.elvss_tth_support_present == TRUE) {
		r = strict_strtoul(buf, 0, &elvss_tth_val);
		if ((r) || ((elvss_tth_val != 0) && (elvss_tth_val != 1))) {
			pr_err("%s: Invalid elvss temp threshold value = %lu\n",
				__func__, elvss_tth_val);
			r = -EINVAL;
			goto end;
		}
		mutex_lock(&mot_panel.lock);
		if (mot_panel.elvss_tth_status != elvss_tth_val)
			mot_panel.elvss_tth_status = elvss_tth_val;
		mutex_unlock(&mot_panel.lock);
	}

end:
	return r ? r : count;

}

static DEVICE_ATTR(elvss_tth_status, S_IRUGO | S_IWGRP,
			elvss_tth_status_show,
			elvss_tth_status_store);
static struct attribute *elvss_tth_attrs[] = {
	&dev_attr_elvss_tth_status.attr,
	NULL,
};
static struct attribute_group elvss_tth_attr_group = {
	.attrs = elvss_tth_attrs,
};

static int valid_mfd_info(struct msm_fb_data_type *mfd)
{
	int ret = 0;

	if (!mfd) {
		pr_err("%s: invalid mfd\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	if (mfd->key != MFD_KEY) {
		pr_err("%s: Invalid key\n", __func__);
		ret = -EINVAL;
		goto err;
	}

	return 0;
err:
	return ret;

}

static int panel_enable(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	pr_info("%s is called\n", __func__);

	mfd = platform_get_drvdata(pdev);

	ret = valid_mfd_info(mfd);
	if (ret != 0)
		goto err;

	if (mot_panel.panel_enable)
		mot_panel.panel_enable(mfd);
	else {
		pr_err("%s: no panel support\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	mipi_set_tx_power_mode(0);
	get_manufacture_id(mfd);
	get_controller_ver(mfd);
	get_controller_drv_ver(mfd);

	pr_info("%s completed. Power_mode =0x%x\n",
				__func__, mipi_mode_get_pwr_mode(mfd));

	return 0;
err:
	return ret;
}

static int panel_disable(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	pr_info("%s is called\n", __func__);

	mfd = platform_get_drvdata(pdev);

	ret = valid_mfd_info(mfd);
	if (ret != 0)
		goto err;

	atomic_set(&mot_panel.state, MOT_PANEL_OFF);
	if (!factory_run && mot_panel.esd_enabled &&
				(mot_panel.esd_detection_run == true)) {
		cancel_delayed_work(&mot_panel.esd_work);
		mot_panel.esd_detection_run = false;
	}

	if (mot_panel.panel_disable)
		mot_panel.panel_disable(mfd);
	else {
		pr_err("%s: no panel support\n", __func__);
		ret = -ENODEV;
		goto err1;
	}

	pr_info("%s completed\n", __func__);

	return 0;
err1:
	atomic_set(&mot_panel.state, MOT_PANEL_ON);
	if (!factory_run && mot_panel.esd_enabled &&
				(mot_panel.esd_detection_run == false)) {
		queue_delayed_work(mot_panel.esd_wq, &mot_panel.esd_work,
						MOT_PANEL_ESD_CHECK_PERIOD);
		mot_panel.esd_detection_run = true;
	}
err:
	pr_err("%s: failed to disable panel\n", __func__);
	return ret;
}

static int panel_on(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	mfd = platform_get_drvdata(pdev);

	ret = valid_mfd_info(mfd);
	if (ret != 0)
		goto err;

	if (mot_panel.panel_on) {
		mot_panel.panel_on(mfd);
		pr_info("MIPI MOT Panel is ON\n");
	}

	atomic_set(&mot_panel.state, MOT_PANEL_ON);
	if (!factory_run && mot_panel.esd_enabled &&
				(mot_panel.esd_detection_run == false)) {
		queue_delayed_work(mot_panel.esd_wq, &mot_panel.esd_work,
						msecs_to_jiffies(20000));
		mot_panel.esd_detection_run = true;
	}
	return 0;
err:
	return ret;
}

static int panel_off(struct platform_device *pdev)
{
	struct msm_fb_data_type *mfd;
	int ret = 0;

	mfd = platform_get_drvdata(pdev);

	ret = valid_mfd_info(mfd);
	if (ret != 0)
		goto err;

	if (mot_panel.panel_off) {
		atomic_set(&mot_panel.state, MOT_PANEL_OFF);
		mot_panel.panel_off(mfd);
		pr_debug("MIPI MOT Panel OFF\n");
	}
	return 0;
err:
	return ret;
}
static void mot_panel_esd_work(struct work_struct *work)
{
	if (mot_panel.esd_run)
		mot_panel.esd_run();
	else
		pr_err("%s: no panel support for ESD\n", __func__);
}

static int mot_panel_off_reboot(struct notifier_block *nb,
			 unsigned long val, void *v)
{
	struct mipi_mot_panel *mot_panel;
	struct msm_fb_data_type *mfd;
	mot_panel = container_of(nb, struct mipi_mot_panel, reboot_notifier);
	mfd = mot_panel->mfd;

	if (mfd->panel_power_on) {

		mutex_lock(&mfd->dma->ov_mutex);

		atomic_set(&mot_panel->state, MOT_PANEL_OFF);
		mipi_mot_mipi_busy_wait(mfd);
		if (mot_panel->panel_disable)
			mot_panel->panel_disable(mfd);

		mutex_unlock(&mfd->dma->ov_mutex);
	}
	return NOTIFY_DONE;
}

static int __devinit mipi_mot_lcd_probe(struct platform_device *pdev)
{
	struct platform_device *lcd_dev;
	int ret = 0;

	if (pdev->id == 0)
		mipi_mot_pdata = pdev->dev.platform_data;

	lcd_dev = msm_fb_add_device(pdev);
	if (!lcd_dev)
		pr_err("%s: Failed to add lcd device\n", __func__);

	mot_panel.mfd = platform_get_drvdata(lcd_dev);
	if (!mot_panel.mfd) {
		pr_err("%s: invalid mfd\n", __func__);
		ret = -ENODEV;
		goto err;
	}

	mutex_init(&mot_panel.lock);
	if (mot_panel.acl_support_present == TRUE) {
		ret = sysfs_create_group(&mot_panel.mfd->fbi->dev->kobj,
                                                       &acl_attr_group);
		if (ret < 0) {
			pr_err("%s: acl_mode file creation failed\n", __func__);
			goto err;
		}
		/* Set the default ACL value to the LCD */
		mot_panel.enable_acl(mot_panel.mfd);
	}

	if (mot_panel.elvss_tth_support_present == TRUE) {
		ret = sysfs_create_group(&mot_panel.mfd->fbi->dev->kobj,
							&elvss_tth_attr_group);
		if (ret < 0) {
			pr_err("%s: elvss_tth_status file creation failed\n",
				 __func__);
			goto err;
		}
	}

	mot_panel.reboot_notifier.notifier_call = mot_panel_off_reboot;
	register_reboot_notifier(&mot_panel.reboot_notifier);

	return 0;
err:
	return ret;
}

static int __devexit mipi_mot_lcd_remove(struct platform_device *pdev)
{
	if (mot_panel.acl_support_present == TRUE) {
		sysfs_remove_group(&mot_panel.mfd->fbi->dev->kobj,
							&acl_attr_group);
	}
	if (mot_panel.reboot_notifier.notifier_call)
		unregister_reboot_notifier(&mot_panel.reboot_notifier);
	mutex_destroy(&mot_panel.lock);
	return 0;
}

static struct platform_driver this_driver = {
	.probe  = mipi_mot_lcd_probe,
	.remove = mipi_mot_lcd_remove,
	.driver = {
		.name   = "mipi_mot",
	},
};

static struct msm_fb_panel_data mot_panel_data = {
	.on		= panel_enable,
	.off		= panel_disable,
	.panel_on	= panel_on,
	.panel_off	= panel_off,
};


struct mipi_mot_panel *mipi_mot_get_mot_panel(void)
{
	return &mot_panel;
}

static int ch_used[3];

int mipi_mot_device_register(struct msm_panel_info *pinfo,
					u32 channel, u32 panel)
{
	struct platform_device *pdev = NULL;
	int ret;

	if ((channel >= 3) || ch_used[channel])
		return -ENODEV;

	ch_used[channel] = TRUE;

	pdev = platform_device_alloc("mipi_mot", (panel << 8)|channel);
	if (!pdev)
		return -ENOMEM;

	mot_panel_data.panel_info = *pinfo;

	if (mot_panel.set_backlight)
		mot_panel_data.set_backlight = mot_panel.set_backlight;

	if (mot_panel.set_backlight_curve)
		mot_panel_data.set_backlight_curve = mot_panel.set_backlight_curve;

	ret = platform_device_add_data(pdev, &mot_panel_data,
		sizeof(mot_panel_data));
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_add_data failed!\n", __func__);
		goto err_device_put;
	}

	ret = platform_device_add(pdev);
	if (ret) {
		printk(KERN_ERR
		  "%s: platform_device_register failed!\n", __func__);
		goto err_device_put;
	}

	if (!factory_run && mot_panel.esd_enabled) {
		mot_panel.esd_wq =
				create_singlethread_workqueue("mot_panel_esd");
		if (mot_panel.esd_wq == NULL) {
			pr_err("%s: failed to create ESD work queue\n",
								__func__);
			goto err_device_put;
		}

		INIT_DELAYED_WORK_DEFERRABLE(&mot_panel.esd_work,
							mot_panel_esd_work);
	} else
		pr_info("MIPI MOT PANEL ESD detection is disable\n");

#ifdef FB_MSM_BOOTLOADER_INIT
	atomic_set(&mot_panel.state, MOT_PANEL_ON);
#else
	atomic_set(&mot_panel.state, MOT_PANEL_OFF);
#endif

	return 0;

err_device_put:
	platform_device_put(pdev);
	return ret;
}

/*
 * This is a work around for now. We need to make this call from board-mmi.c
 * but there is no way for this panel to make the call to this file. It has
 * to make the call to the msm_fb and from there, it will call to board-mmi.c
 * because we have to support the factory build which the ESD will not run
 * because there might be not have the panel
 */
extern int mot_panel_is_factory_mode(void);
static int __init mipi_mot_lcd_init(void)
{
	mipi_dsi_buf_alloc(&mot_tx_buf, DSI_BUF_SIZE);
	mipi_dsi_buf_alloc(&mot_rx_buf, DSI_BUF_SIZE);

	mot_panel.mot_tx_buf = &mot_tx_buf;
	mot_panel.mot_rx_buf = &mot_rx_buf;

	mipi_mot_set_mot_panel(&mot_panel);
	mot_panel.get_manufacture_id = mipi_mot_get_manufacture_id;
	mot_panel.get_controller_ver = mipi_mot_get_controller_ver;
	mot_panel.get_controller_drv_ver = mipi_mot_get_controller_drv_ver;
	mot_panel.esd_run = mipi_mot_esd_work;

	mot_panel.panel_on = mipi_mot_panel_on;
	mot_panel.panel_off = NULL;

	factory_run = mot_panel_is_factory_mode();
	if (factory_run)
		pr_info("MIPI MOT PANEL: Factory mode\n");

	return platform_driver_register(&this_driver);
}

module_init(mipi_mot_lcd_init);
