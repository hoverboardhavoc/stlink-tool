/*
  Copyright (c) 2018 Jean THOMAS.
  
  Permission is hereby granted, free of charge, to any person obtaining
  a copy of this software and associated documentation files (the "Software"),
  to deal in the Software without restriction, including without limitation
  the rights to use, copy, modify, merge, publish, distribute, sublicense,
  and/or sell copies of the Software, and to permit persons to whom the Software
  is furnished to do so, subject to the following conditions:
  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.
  
  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
  EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
  OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
  CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
  TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE
  OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stdint.h>
#include <libusb.h>

#include "stlink.h"

#define STLINK_VID 0x0483
#define STLINK_PID 0x3748

void print_help(char *argv[]) {
  printf("Usage: %s [options] [firmware.bin]\n", argv[0]);
  printf("Options:\n");
  printf("\t-p\tProbe the ST-Link adapter\n");
  printf("\t-l\tTarget ST-Link by USB location, e.g. -l 1-2\n");
  printf("\t-h\tShow help\n\n");
  printf("\tApplication is started when called without argument or after firmware load\n\n");
}

static libusb_device_handle *open_stlink(libusb_context *ctx, const char *loc) {
  libusb_device **list = NULL;
  ssize_t n = libusb_get_device_list(ctx, &list);
  libusb_device_handle *h = NULL;
  int matches = 0;
  int want_bus = -1, want_nports = 0;
  uint8_t want_ports[8];

  if (loc) {
    want_bus = atoi(loc);
    const char *dash = strchr(loc, '-');
    if (dash) {
      char buf[64];
      strncpy(buf, dash + 1, sizeof(buf) - 1);
      buf[sizeof(buf) - 1] = '\0';
      char *tok = strtok(buf, ".");
      while (tok && want_nports < 8) {
        want_ports[want_nports++] = (uint8_t)atoi(tok);
        tok = strtok(NULL, ".");
      }
    }
  }

  for (ssize_t i = 0; i < n; i++) {
    struct libusb_device_descriptor desc;
    if (libusb_get_device_descriptor(list[i], &desc)) continue;
    if (desc.idVendor != STLINK_VID || desc.idProduct != STLINK_PID) continue;

    int bus = libusb_get_bus_number(list[i]);
    uint8_t ports[8];
    int np = libusb_get_port_numbers(list[i], ports, 8);

    if (loc) {
      if (bus != want_bus || np != want_nports) continue;
      int ok = 1;
      for (int k = 0; k < np; k++) if (ports[k] != want_ports[k]) ok = 0;
      if (!ok) continue;
    }

    matches++;
    printf("ST-Link at %d-", bus);
    for (int k = 0; k < np; k++) printf("%s%d", k ? "." : "", ports[k]);
    printf("\n");

    if (!h) {
      if (libusb_open(list[i], &h)) h = NULL;
    }
  }

  libusb_free_device_list(list, 1);

  if (!loc && matches > 1) {
    fprintf(stderr, "Multiple ST-Links present (%d). Refusing to guess; pass -l <bus-port> (e.g. -l 1-2).\n", matches);
    if (h) libusb_close(h);
    return NULL;
  }
  if (matches == 0) {
    fprintf(stderr, "No matching ST-Link found%s%s.\n", loc ? " at location " : "", loc ? loc : "");
  }
  return h;
}

int main(int argc, char *argv[]) {
  libusb_context *usb_ctx;
  libusb_device_handle *dev_handle;
  struct STLinkInfos infos;
  int res, i, opt, probe = 0;
  char *location = NULL;

  while ((opt = getopt(argc, argv, "hpl:")) != -1) {
    switch (opt) {
    case 'p': /* Probe mode */
      probe = 1;
      break;
    case 'l': /* Target USB location bus-port[.port...] */
      location = optarg;
      break;
    case 'h': /* Help */
      print_help(argv);
      return EXIT_SUCCESS;
      break;
    default:
      print_help(argv);
      return EXIT_FAILURE;
      break;
    }
  }

  int do_load = (optind < argc);

  res = libusb_init(&usb_ctx);
  (void)res;

  dev_handle = open_stlink(usb_ctx, location);
  if (!dev_handle) {
    return EXIT_FAILURE;
  }

  if (libusb_claim_interface(dev_handle, 0)) {
    fprintf(stderr, "Unable to claim USB interface ! Please close all programs that may communicate with an ST-Link dongle.\n");
    return EXIT_FAILURE;
  }

  if (stlink_read_infos(dev_handle, &infos)) {
    libusb_release_interface(dev_handle, 0);
    return EXIT_FAILURE;
  }
  
  printf("Firmware version : V%dJ%dS%d\n", infos.stlink_version,
	 infos.jtag_version, infos.swim_version);
  printf("Loader version : %d\n", infos.loader_version);
  printf("ST-Link ID : ");
  for (i = 0; i < 12; i++) {
    printf("%02X", infos.id[i]);
  }
  printf("\n");
  printf("Firmware encryption key : ");
  for (i = 0; i < 16; i++) {
    printf("%02X", infos.firmware_key[i]);
  }
  printf("\n");

  res = stlink_current_mode(dev_handle);
  if (res < 0) {
    libusb_release_interface(dev_handle, 0);
    return EXIT_FAILURE;
  }
  printf("Current mode : %d\n", res);

  if (res != 1) {
    printf("ST-Link dongle is not in the correct mode. Please unplug and plug the dongle again.\n");
    libusb_release_interface(dev_handle, 0);
    return EXIT_SUCCESS;
  }

  if (!probe) {
    if (do_load) {
      stlink_flash(dev_handle, argv[optind], 0x8004000, 1024, &infos);
    }
    stlink_exit_dfu(dev_handle);
  }

  libusb_release_interface(dev_handle, 0);
  libusb_exit(usb_ctx);

  return EXIT_SUCCESS;
}
