
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2013, the olsr.org team - see HISTORY file
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * * Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 * * Neither the name of olsr.org, olsrd nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * Visit http://www.olsr.org for more information.
 *
 * If you find this software useful feel free to make a donation
 * to the project. For more information see the website or contact
 * the copyright holders.
 *
 */

#ifndef DLEP_ROUTER_H_
#define DLEP_ROUTER_H_

#include "common/common_types.h"
#include "common/avl.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_packet_socket.h"
#include "subsystems/oonf_timer.h"

enum dlep_router_state {
  DLEP_ROUTER_DISCOVERY,
  DLEP_ROUTER_CONNECT,
  DLEP_ROUTER_ACTIVE,
};

struct dlep_router_session {
  /* interface name to talk with DLEP radio */
  char interf[IF_NAMESIZE];

  /* state of the DLEP session */
  enum dlep_router_state state;

  /* UDP socket for discovery */
  struct oonf_packet_managed discovery;
  struct oonf_packet_managed_config discovery_config;

  /* event timer (either discovery, connect timeout or heartbeat) */
  struct oonf_timer_entry event_timer;

  /* timeout (connect timeout or heartbeat timeout) */
  struct oonf_timer_entry timeout;

  /* timer settings */
  uint64_t discovery_interval;
  uint64_t heartbeat_interval;

  /* hook into session tree, interface name is the key */
  struct avl_node _node;
};

#define LOG_DLEP_ROUTER dlep_router_subsystem.logging
EXPORT extern struct oonf_subsystem dlep_router_subsystem;

#endif /* DLEP_ROUTER_H_ */
