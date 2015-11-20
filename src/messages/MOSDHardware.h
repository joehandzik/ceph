// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*- 
// vim: ts=8 sw=2 smarttab
/*
 * Ceph - scalable distributed file system
 *
 * Copyright (c) 2015 Hewlett Packard Enterprise Development LP
 *
 * This is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License version 2.1, as published by the Free Software 
 * Foundation.  See file COPYING.
 * 
 */


#ifndef CEPH_MOSDHARDWARE_H
#define CEPH_MOSDHARDWARE_H

#include "msg/Message.h"

/*
 * Execute some sort of operation against hardware that an OSD can interact
 * with.
 */

struct MOSDHardware : public Message {

  static const int HEAD_VERSION = 1;
  static const int COMPAT_VERSION = 1;

  uuid_d fsid;
  string hardware_str;
  string operation_str;

  MOSDHardware() : Message(MSG_OSD_HARDWARE, HEAD_VERSION, COMPAT_VERSION) {}
  MOSDHardware(const uuid_d& f, string h, string o) :
    Message(MSG_OSD_HARDWARE, HEAD_VERSION, COMPAT_VERSION),
    fsid(f), hardware_str(h), operation_str(o) {}
private:
  ~MOSDHardware() {}

public:
  const char *get_type_name() const { return "hardware"; }
  void print(ostream& out) const {
    out << "hardware(";
    out << hardware_str << " ";
    out << operation_str << " ";
    out << ")";
  }

  void encode_payload(uint64_t features) {
    ::encode(fsid, payload);
    ::encode(hardware_str, payload);
    ::encode(operation_str, payload);
  }
  void decode_payload() {
    bufferlist::iterator p = payload.begin();
    ::decode(fsid, p);
    ::decode(hardware_str, p);
    ::decode(operation_str, p);
  }
};

#endif
