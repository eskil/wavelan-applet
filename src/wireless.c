#include "wireless.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

static WirelessInfo*
wi_new (const char *dev, int link, int level, int noise) {
  WirelessInfo *res = g_new0 (WirelessInfo, 1);
  
  res->device = g_strdup (dev);
  res->link = link;
  res->level = level;
  res->noise = noise;
  if (link) {
    res->percent = (int)rint ((log (link) / log (92)) * 100.0);
  } else {
    res->percent = 0;
  }
  return res;
}

static void
wi_copy (WirelessInfo *a, WirelessInfo *b) {
  a->device = g_strdup (b->device);
  a->link = b->link;
  a->level = b->level;
  a->noise = b->noise;
  a->percent = b->percent;
}

static void 
wi_destroy (WirelessInfo *wi) {
  g_free (wi->device);
  g_free (wi);
}

static GList*
wireless_get_all_devices (Wireless *w) {
  GList *ret = NULL;
  long int level, noise;
  double link;
  char device[256];
  char line[256];
  
  g_assert (w);

  /* Here we begin to suck... */
  do {
    char *ptr;
    
    fgets (line, 256, w->file);
    
    if (feof (w->file)) {
      break;
		}
    
    if (line[6] == ':') {
      WirelessInfo *wi;
      char *tptr = line;
      while (isspace (*tptr)) tptr++;
      strncpy (device, tptr, 6);
      (*strchr(device, ':')) = 0;
      ptr = line + 12;
      
      /*
	g_message ("prop->dev = %s, dev = %s, cmp = %d", 
	properties->device, device,
	g_strcasecmp (properties->device, device));			
      */

      link = strtod (ptr, &ptr);
      ptr++;
      
      level = strtol (ptr, &ptr, 10);
      ptr++;
      
      noise = strtol (ptr, &ptr, 10);
      ptr++;
      
      wi = wi_new (device, link, level, noise);
      ret = g_list_prepend (ret, wi);
    }
  } while (1);
  
  /* rewind the /proc/net/wireless file */
  rewind (w->file);
  return ret;
}

Wireless*
wireless_new (void) {
  Wireless *ret = g_new0 (Wireless, 1);
  ret->file = fopen ("/proc/net/wireless", "r");
  return ret;
}

void
wireless_destroy (Wireless *w) {
  g_free (w);
}

gboolean
wireless_ok (Wireless *w) {
  return w->file != NULL;
}

GList*
wireless_get_devices (Wireless *w) {
  GList *devices = NULL;
  GList *alldevices = wireless_get_all_devices (w);
  GList *ptr;
  for (ptr = alldevices; ptr; ptr = g_list_next (ptr)) {
    WirelessInfo *wi = (WirelessInfo*)(ptr->data);
    devices = g_list_prepend (devices, g_strdup (wi->device));
    wi_destroy (wi);
  }
  g_list_free (alldevices);
  return devices;
}

WirelessInfo
wireless_get_device_state (Wireless *w, const char *device)
{
  WirelessInfo result;
  GList *alldevices = wireless_get_all_devices (w);
  GList *ptr;
  for (ptr = alldevices; ptr; ptr = g_list_next (ptr)) {
    WirelessInfo *wi = (WirelessInfo*)(ptr->data);
    if (strcasecmp (wi->device, device) == 0) { 
      wi_copy (&result, wi);
      /* but don't break out, since we still need to free all devices */
    };
    wi_destroy (wi);
  }
  g_list_free (alldevices);
  return result;
}
