#pragma once

#include "upnp.h"

#include <Arduino.h>

/** mDNS browse for _googlecast._tcp; appends Cast entries into the shared LAN list. */
void castDiscoverInto(std::vector<UpnpRenderer> *devs, size_t max_total);

/** Refresh / control the Cast device described by r (source == Cast). */
bool castRefresh(UpnpRenderer *r);
bool castPlay(UpnpRenderer *r);
bool castPause(UpnpRenderer *r);
bool castNext(UpnpRenderer *r);
bool castPrevious(UpnpRenderer *r);
void castDisconnect();

const char *castLastError();
