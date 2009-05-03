/*
 * "Ingenic flash tool" - flash the Ingenic CPU via USB
 *
 * (C) Copyright 2009
 * Author: Marek Lindner <lindner_marek@yahoo.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 3 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA
 */

#include "ingenic_usb.h"
#include "usb_boot_defines.h"
#include <usb.h>
#include <stdio.h>
#include <string.h>

extern unsigned int total_size;

static int get_ingenic_device(struct ingenic_dev *ingenic_dev)
{
	struct usb_bus *usb_busses, *usb_bus;
	struct usb_device *usb_dev;
	int count = 0;

	usb_busses = usb_get_busses();

	for (usb_bus = usb_busses; usb_bus != NULL; usb_bus = usb_bus->next) {
		for (usb_dev = usb_bus->devices; usb_dev != NULL; usb_dev = usb_dev->next) {

			if ((usb_dev->descriptor.idVendor == VENDOR_ID) &&
				(usb_dev->descriptor.idProduct == PRODUCT_ID)) {
				ingenic_dev->usb_dev = usb_dev;
				count++;
			}

		}
	}

	return count;
}

static int get_ingenic_interface(struct ingenic_dev *ingenic_dev)
{
	struct usb_config_descriptor *usb_config_desc;
	struct usb_interface_descriptor *usb_if_desc;
	struct usb_interface *usb_if;
	int config_index, if_index, alt_index;

	for (config_index = 0; config_index < ingenic_dev->usb_dev->descriptor.bNumConfigurations; config_index++) {
		usb_config_desc = &ingenic_dev->usb_dev->config[config_index];

		if (!usb_config_desc)
			return 0;

		for (if_index = 0; if_index < usb_config_desc->bNumInterfaces; if_index++) {
			usb_if = &usb_config_desc->interface[if_index];

			if (!usb_if)
				return 0;

			for (alt_index = 0; alt_index < usb_if->num_altsetting; alt_index++) {
				usb_if_desc = &usb_if->altsetting[alt_index];

				if (!usb_if_desc)
					return 0;

				if ((usb_if_desc->bInterfaceClass == 0xff) &&
					(usb_if_desc->bInterfaceSubClass == 0)) {
					ingenic_dev->interface = usb_if_desc->bInterfaceNumber;
					return 1;
				}
			}
		}
	}

	return 0;
}

int usb_ingenic_init(struct ingenic_dev *ingenic_dev)
{
	int num_ingenic, status = -1;

	memset(ingenic_dev, 0, sizeof(struct ingenic_dev));

	usb_init();
 	/* usb_set_debug(255); */
	usb_find_busses();
	usb_find_devices();

	num_ingenic = get_ingenic_device(ingenic_dev);

	if (num_ingenic == 0) {
		fprintf(stderr, "Error - no Ingenic device found\n");
		goto out;
	}

	if (num_ingenic > 1) {
		fprintf(stderr, "Error - too many Ingenic devices found: %i\n", num_ingenic);
		goto out;
	}

	ingenic_dev->usb_handle = usb_open(ingenic_dev->usb_dev);
	if (!ingenic_dev->usb_handle) {
		fprintf(stderr, "Error - can't open Ingenic device: %s\n", usb_strerror());
		goto out;
	}

	if (usb_set_configuration(ingenic_dev->usb_handle, 1) < 0) {
		fprintf(stderr, "Error - can't set Ingenic configuration: %s\n", usb_strerror());
		goto out;
	}

	if (get_ingenic_interface(ingenic_dev) < 1) {
		fprintf(stderr, "Error - can't find Ingenic interface\n");
		goto out;
	}

	if (usb_claim_interface(ingenic_dev->usb_handle, ingenic_dev->interface) < 0) {
		fprintf(stderr, "Error - can't claim Ingenic interface: %s\n", usb_strerror());
		goto out;
	}

	status = 1;

out:
	return status;
}

int usb_get_ingenic_cpu(struct ingenic_dev *ingenic_dev)
{
	int status;

	memset(&ingenic_dev->cpu_info_buff, 0, 
	       ARRAY_SIZE(ingenic_dev->cpu_info_buff));

	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_IN | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ VR_GET_CPU_INFO,
          /* wValue        */ 0,
          /* wIndex        */ 0,
          /* Data          */ ingenic_dev->cpu_info_buff,
          /* wLength       */ 8,
                              USB_TIMEOUT);

	if (status != sizeof(ingenic_dev->cpu_info_buff) - 1 ) {
		fprintf(stderr, "Error - can't retrieve Ingenic CPU information: %i\n", status);
		return status;
	}

	ingenic_dev->cpu_info_buff[8] = '\0';
	printf("\n CPU data: %02x, %02x, %02x, %02x, %02x, %02x, %02x, %02x : %s\n",
		ingenic_dev->cpu_info_buff[0], ingenic_dev->cpu_info_buff[1],
		ingenic_dev->cpu_info_buff[2], ingenic_dev->cpu_info_buff[3],
		ingenic_dev->cpu_info_buff[4], ingenic_dev->cpu_info_buff[5],
		ingenic_dev->cpu_info_buff[6], ingenic_dev->cpu_info_buff[7],
		ingenic_dev->cpu_info_buff);

	if (!strcmp(ingenic_dev->cpu_info_buff,"JZ4740V1")) return 1;
	if (!strcmp(ingenic_dev->cpu_info_buff,"JZ4750V1")) return 2;
	if (!strcmp(ingenic_dev->cpu_info_buff,"Boot4740")) return 3;
	if (!strcmp(ingenic_dev->cpu_info_buff,"Boot4750")) return 4;

	return 0;
}

int usb_ingenic_flush_cache(struct ingenic_dev *ingenic_dev)
{
	int status;

	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ VR_FLUSH_CACHES,
          /* wValue        */ 0,
          /* wIndex        */ 0,
          /* Data          */ 0,
          /* wLength       */ 0,
                              USB_TIMEOUT);

	if (status != 0) {
		fprintf(stderr, "Error - can't flush cache: %i\n", status);
		return status;
	}

	return 1;
}

int usb_send_data_length_to_ingenic(struct ingenic_dev *ingenic_dev, int len)
{
	int status;
	/* tell the device the length of the file to be uploaded */
	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ VR_SET_DATA_LENGTH,
          /* wValue        */ 0,
          /* wIndex        */ len,
          /* Data          */ 0,
          /* wLength       */ 0,
                              USB_TIMEOUT);

	if (status != 0)
		fprintf(stderr, "Error - can't set data length on Ingenic device: %i\n", status);

	return status;
}

int usb_send_data_address_to_ingenic(struct ingenic_dev *ingenic_dev, unsigned int stage_addr)
{
	int status;
	/* tell the device the RAM address to store the file */
	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ VR_SET_DATA_ADDRESS,
          /* wValue        */ STAGE_ADDR_MSB(stage_addr),
          /* wIndex        */ STAGE_ADDR_LSB(stage_addr),
          /* Data          */ 0,
          /* wLength       */ 0,
                              USB_TIMEOUT);

	if (status != 0) {
		fprintf(stderr, "Error - can't set the address on Ingenic device: %i\n", status);
		return -1;
	}

	return status;
}

int usb_send_data_to_ingenic(struct ingenic_dev *ingenic_dev)
{
	int status;
	status = usb_bulk_write(ingenic_dev->usb_handle,
	/* endpoint         */ INGENIC_OUT_ENDPOINT,
	/* bulk data        */ ingenic_dev->file_buff,
	/* bulk data length */ ingenic_dev->file_len,
				USB_TIMEOUT);
	if (status < ingenic_dev->file_len) {
		fprintf(stderr, "Error - can't send bulk data to Ingenic CPU: %i\n", status);
		return -1;
	}
	return 1;
}

int usb_ingenic_upload(struct ingenic_dev *ingenic_dev, int stage)
{
	int status;

	unsigned int stage2_addr;
	stage2_addr = total_size + 0x80000000;
	stage2_addr -= CODE_SIZE;
	int stage_addr = (stage == 1 ? 0x80002000 : stage2_addr);

	usb_send_data_address_to_ingenic(ingenic_dev, stage_addr);
	printf("\n Download stage %d program and execute at 0x%08x ", stage, (stage_addr));
	usb_send_data_to_ingenic(ingenic_dev);

	if (stage == 2) {
		sleep(1);
		usb_get_ingenic_cpu(ingenic_dev);
		usb_ingenic_flush_cache(ingenic_dev);
	}

	/* tell the device to start the uploaded device */
	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ (stage == 1 ? VR_PROGRAM_START1 : VR_PROGRAM_START2),
          /* wValue        */ STAGE_ADDR_MSB(stage_addr),
          /* wIndex        */ STAGE_ADDR_LSB(stage_addr),
          /* Data          */ 0,
          /* wLength       */ 0,
                              USB_TIMEOUT);

	if (status != 0) {
		fprintf(stderr, "Error - can't start the uploaded binary on the Ingenic device: %i\n", status);
		goto out;
	}

	sleep(1);
	usb_get_ingenic_cpu(ingenic_dev);
	status = 1;

out:
	return status;
}

void usb_ingenic_cleanup(struct ingenic_dev *ingenic_dev)
{
	if ((ingenic_dev->usb_handle) && (ingenic_dev->interface))
		usb_release_interface(ingenic_dev->usb_handle, ingenic_dev->interface);

	if (ingenic_dev->usb_handle)
		usb_close(ingenic_dev->usb_handle);
}

int usb_ingenic_nand_ops(struct ingenic_dev *ingenic_dev, int ops)
{
	int status;
	status = usb_control_msg(ingenic_dev->usb_handle,
          /* bmRequestType */ USB_ENDPOINT_OUT | USB_TYPE_VENDOR | USB_RECIP_DEVICE,
          /* bRequest      */ VR_NAND_OPS,
          /* wValue        */ ops & 0xffff,
          /* wIndex        */ 0,
          /* Data          */ 0,
          /* wLength       */ 0,
                              USB_TIMEOUT);

	if (status != 0) {
		fprintf(stderr, "Error - can't set Ingenic device nand ops: %i\n", status);
		return -1;
	}

	return status;
}