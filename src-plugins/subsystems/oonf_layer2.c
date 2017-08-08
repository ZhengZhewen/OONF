
/*
 * The olsr.org Optimized Link-State Routing daemon version 2 (olsrd2)
 * Copyright (c) 2004-2015, the olsr.org team - see HISTORY file
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

/**
 * @file
 */

#include "common/avl.h"
#include "common/avl_comp.h"
#include "common/common_types.h"
#include "common/netaddr.h"
#include "config/cfg_schema.h"
#include "core/oonf_subsystem.h"
#include "subsystems/oonf_class.h"
#include "subsystems/os_interface.h"

#include "subsystems/oonf_layer2.h"

/* Definitions */
#define LOG_LAYER2 _oonf_layer2_subsystem.logging

/* prototypes */
static int _init(void);
static void _cleanup(void);

static void _net_remove(struct oonf_layer2_net *l2net);
static void _neigh_remove(struct oonf_layer2_neigh *l2neigh);

/* subsystem definition */
static const char *_dependencies[] = {
  OONF_CLASS_SUBSYSTEM,
  OONF_OS_INTERFACE_SUBSYSTEM,
};

static struct oonf_subsystem _oonf_layer2_subsystem = {
  .name = OONF_LAYER2_SUBSYSTEM,
  .dependencies = _dependencies,
  .dependencies_count = ARRAYSIZE(_dependencies),
  .init = _init,
  .cleanup = _cleanup,
};
DECLARE_OONF_PLUGIN(_oonf_layer2_subsystem);

/* layer2 neighbor metadata */
static const struct oonf_layer2_metadata _oonf_layer2_metadata_neigh[OONF_LAYER2_NEIGH_COUNT] = {
  [OONF_LAYER2_NEIGH_TX_SIGNAL]      = { .key = "tx_signal", .unit = "dBm", .fraction = 3 },
  [OONF_LAYER2_NEIGH_RX_SIGNAL]      = { .key = "rx_signal", .unit = "dBm", .fraction = 3 },
  [OONF_LAYER2_NEIGH_TX_BITRATE]     = { .key = "tx_bitrate", .unit = "bit/s", .binary = true },
  [OONF_LAYER2_NEIGH_RX_BITRATE]     = { .key = "rx_bitrate", .unit = "bit/s", .binary = true },
  [OONF_LAYER2_NEIGH_TX_MAX_BITRATE] = { .key = "tx_max_bitrate", .unit = "bit/s", .binary = true },
  [OONF_LAYER2_NEIGH_RX_MAX_BITRATE] = { .key = "rx_max_bitrate", .unit = "bit/s", .binary = true },
  [OONF_LAYER2_NEIGH_TX_BYTES]       = { .key = "tx_bytes", .unit = "byte", .binary = true },
  [OONF_LAYER2_NEIGH_RX_BYTES]       = { .key = "rx_bytes", .unit = "byte", .binary = true },
  [OONF_LAYER2_NEIGH_TX_FRAMES]      = { .key = "tx_frames" },
  [OONF_LAYER2_NEIGH_RX_FRAMES]      = { .key = "rx_frames" },
  [OONF_LAYER2_NEIGH_TX_THROUGHPUT]  = { .key = "tx_throughput", .unit = "bit/s", .binary = true },
  [OONF_LAYER2_NEIGH_TX_RETRIES]     = { .key = "tx_retries" },
  [OONF_LAYER2_NEIGH_TX_FAILED]      = { .key = "tx_failed" },
  [OONF_LAYER2_NEIGH_LATENCY]        = { .key = "latency", .unit = "s", .fraction = 6 },
  [OONF_LAYER2_NEIGH_RESOURCES]      = { .key = "resources" },
  [OONF_LAYER2_NEIGH_TX_RLQ]         = { .key = "tx_rlq" },
  [OONF_LAYER2_NEIGH_RX_RLQ]         = { .key = "rx_rlq" },
};

/* layer2 network metadata */
static const struct oonf_layer2_metadata _oonf_layer2_metadata_net[OONF_LAYER2_NET_COUNT] = {
  [OONF_LAYER2_NET_FREQUENCY_1]     = { .key = "frequency1", .unit = "Hz" },
  [OONF_LAYER2_NET_FREQUENCY_2]     = { .key = "frequency2", .unit = "Hz" },
  [OONF_LAYER2_NET_BANDWIDTH_1]     = { .key = "bandwidth1", .unit = "Hz" },
  [OONF_LAYER2_NET_BANDWIDTH_2]     = { .key = "bandwidth2", .unit = "Hz" },
  [OONF_LAYER2_NET_NOISE]           = { .key = "noise", .unit="dBm", .fraction = 3 },
  [OONF_LAYER2_NET_CHANNEL_ACTIVE]  = { .key = "ch_active", .unit="s", .fraction = 9 },
  [OONF_LAYER2_NET_CHANNEL_BUSY]    = { .key = "ch_busy", .unit="s", .fraction = 9 },
  [OONF_LAYER2_NET_CHANNEL_RX]      = { .key = "ch_rx", .unit="s", .fraction = 9 },
  [OONF_LAYER2_NET_CHANNEL_TX]      = { .key = "ch_tx", .unit="s", .fraction = 9 },
  [OONF_LAYER2_NET_MTU]             = { .key = "mtu", .unit="byte" },
};

static const char *oonf_layer2_network_type[OONF_LAYER2_TYPE_COUNT] = {
  [OONF_LAYER2_TYPE_UNDEFINED] = "undefined",
  [OONF_LAYER2_TYPE_WIRELESS]  = "wireless",
  [OONF_LAYER2_TYPE_ETHERNET]  = "ethernet",
  [OONF_LAYER2_TYPE_TUNNEL]    = "tunnel",
};

/* infrastructure for l2net/l2neigh tree */
static struct oonf_class _l2network_class = {
  .name = LAYER2_CLASS_NETWORK,
  .size = sizeof(struct oonf_layer2_net),
};
static struct oonf_class _l2neighbor_class = {
  .name = LAYER2_CLASS_NEIGHBOR,
  .size = sizeof(struct oonf_layer2_neigh),
};
static struct oonf_class _l2dst_class = {
  .name = LAYER2_CLASS_DESTINATION,
  .size = sizeof(struct oonf_layer2_destination),
};
static struct oonf_class _l2net_addr_class = {
  .name = LAYER2_CLASS_NETWORK_ADDRESS,
  .size = sizeof(struct oonf_layer2_peer_address),
};
static struct oonf_class _l2neigh_addr_class = {
  .name = LAYER2_CLASS_NEIGHBOR_ADDRESS,
  .size = sizeof(struct oonf_layer2_neighbor_address),
};

static struct avl_tree _oonf_layer2_net_tree;

static struct avl_tree _oonf_originator_tree;

/**
 * Subsystem constructor
 * @return always returns 0
 */
static int
_init(void) {
  oonf_class_add(&_l2network_class);
  oonf_class_add(&_l2neighbor_class);
  oonf_class_add(&_l2dst_class);
  oonf_class_add(&_l2net_addr_class);
  oonf_class_add(&_l2neigh_addr_class);

  avl_init(&_oonf_layer2_net_tree, avl_comp_strcasecmp, false);
  avl_init(&_oonf_originator_tree, avl_comp_strcasecmp, false);
  return 0;
}

/**
 * Subsystem destructor
 */
static void
_cleanup(void) {
  struct oonf_layer2_net *l2net, *l2n_it;

  avl_for_each_element_safe(&_oonf_layer2_net_tree, l2net, _node, l2n_it) {
    _net_remove(l2net);
  }

  oonf_class_remove(&_l2neigh_addr_class);
  oonf_class_remove(&_l2net_addr_class);
  oonf_class_remove(&_l2dst_class);
  oonf_class_remove(&_l2neighbor_class);
  oonf_class_remove(&_l2network_class);
}

/**
 * Register a new data originator number for layer2 data
 * @param origin layer2 originator
 */
void
oonf_layer2_add_origin(struct oonf_layer2_origin *origin) {
  origin->_node.key = origin->name;
  avl_insert(&_oonf_originator_tree, &origin->_node);
}

/**
 * Removes all layer2 data associated with this data originator
 * @param origin originator
 */
void
oonf_layer2_remove_origin(struct oonf_layer2_origin *origin) {
  struct oonf_layer2_net *l2net, *l2net_it;

  if (!avl_is_node_added(&origin->_node)) {
    return;
  }

  avl_for_each_element_safe(&_oonf_layer2_net_tree, l2net, _node, l2net_it) {
    oonf_layer2_net_remove(l2net, origin);
  }

  avl_remove(&_oonf_originator_tree, &origin->_node);
}

/**
 * Add a layer-2 network to the database
 * @param ifname name of interface
 * @return layer-2 network object
 */
struct oonf_layer2_net *
oonf_layer2_net_add(const char *ifname) {
  struct oonf_layer2_net *l2net;

  if (!ifname) {
    return NULL;
  }

  l2net = avl_find_element(&_oonf_layer2_net_tree, ifname, l2net, _node);
  if (l2net) {
    return l2net;
  }

  l2net = oonf_class_malloc(&_l2network_class);
  if (!l2net) {
    return NULL;
  }

  /* initialize key */
  strscpy(l2net->name, ifname, sizeof(l2net->name));

  /* add to global l2net tree */
  l2net->_node.key = l2net->name;
  avl_insert(&_oonf_layer2_net_tree, &l2net->_node);

  /* initialize tree of neighbors, ips and proxies */
  avl_init(&l2net->neighbors, avl_comp_netaddr, false);
  avl_init(&l2net->local_peer_ips, avl_comp_netaddr, false);

  /* initialize interface listener */
  l2net->if_listener.name = l2net->name;
  os_interface_add(&l2net->if_listener);

  oonf_class_event(&_l2network_class, l2net, OONF_OBJECT_ADDED);

  return l2net;
}

/**
 * Remove all data objects of a certain originator from a layer-2 network
 * object.
 * @param l2net layer-2 addr object
 * @param origin originator number
 * @param cleanup_neigh true to cleanup neighbor data too
 * @return true if a value was removed, false otherwise
 */
bool
oonf_layer2_net_cleanup(struct oonf_layer2_net *l2net,
    const struct oonf_layer2_origin *origin, bool cleanup_neigh) {
  struct oonf_layer2_neigh *l2neigh;
  bool changed = false;
  int i;

  for (i=0; i<OONF_LAYER2_NET_COUNT; i++) {
    if (l2net->data[i]._origin == origin) {
      oonf_layer2_reset_value(&l2net->data[i]);
      changed = true;
    }
  }
  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (l2net->neighdata[i]._origin == origin) {
      oonf_layer2_reset_value(&l2net->neighdata[i]);
      changed = true;
    }
  }

  if (cleanup_neigh) {
    avl_for_each_element(&l2net->neighbors, l2neigh, _node) {
      changed |= oonf_layer2_neigh_cleanup(l2neigh, origin);
    }
  }
  return changed;
}

/**
 * Remove all information of a certain originator from a layer-2 addr
 * object. Remove the object if its empty and has no neighbors anymore.
 * @param l2net layer-2 addr object
 * @param origin originator identifier
 * @return true if something changed, false otherwise
 */
bool
oonf_layer2_net_remove(struct oonf_layer2_net *l2net,
    const struct oonf_layer2_origin *origin) {
  struct oonf_layer2_neigh *l2neigh, *l2neigh_it;
  bool changed = false;

  if (!avl_is_node_added(&l2net->_node)) {
    return false;
  }

  avl_for_each_element_safe(&l2net->neighbors, l2neigh, _node, l2neigh_it) {
    if (oonf_layer2_neigh_remove(l2neigh, origin)) {
      changed = true;
    }
  }

  if (oonf_layer2_net_cleanup(l2net, origin, false)) {
    changed = true;
  }

  if (changed) {
    oonf_layer2_net_commit(l2net);
  }
  return changed;
}

/**
 * Commit all changes to a layer-2 addr object. This might remove the
 * object from the database if all data has been removed from the object.
 * @param l2net layer-2 addr object
 * @return true if the object has been removed, false otherwise
 */
bool
oonf_layer2_net_commit(struct oonf_layer2_net *l2net) {
  size_t i;

  if (l2net->neighbors.count > 0) {
    oonf_class_event(&_l2network_class, l2net, OONF_OBJECT_CHANGED);
    return false;
  }

  for (i=0; i<OONF_LAYER2_NET_COUNT; i++) {
    if (oonf_layer2_has_value(&l2net->data[i])) {
      oonf_class_event(&_l2network_class, l2net, OONF_OBJECT_CHANGED);
      return false;
    }
  }

  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (oonf_layer2_has_value(&l2net->neighdata[i])) {
      oonf_class_event(&_l2network_class, l2net, OONF_OBJECT_CHANGED);
      return false;
    }
  }

  _net_remove(l2net);
  return true;
}

/**
 * Relabel all network data (including neighbor data)
 * of one origin to another one
 * @param l2net layer2 network object
 * @param new_origin new origin
 * @param old_origin old origin to overwrite
 */
void
oonf_layer2_net_relabel(struct oonf_layer2_net *l2net,
    const struct oonf_layer2_origin *new_origin,
    const struct oonf_layer2_origin *old_origin) {
  struct oonf_layer2_neigh *l2neigh;
  struct oonf_layer2_peer_address *peer_ip;
  size_t i;

  for (i=0; i<OONF_LAYER2_NET_COUNT; i++) {
    if (oonf_layer2_get_origin(&l2net->data[i]) == old_origin) {
      oonf_layer2_set_origin(&l2net->data[i], new_origin);
    }
  }

  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (oonf_layer2_get_origin(&l2net->neighdata[i]) == old_origin) {
      oonf_layer2_set_origin(&l2net->neighdata[i], new_origin);
    }
  }

  avl_for_each_element(&l2net->local_peer_ips, peer_ip,_node) {
    if (peer_ip->origin == old_origin) {
      peer_ip->origin = new_origin;
    }
  }

  avl_for_each_element(&l2net->neighbors, l2neigh, _node) {
    oonf_layer2_neigh_relabel(l2neigh, new_origin, old_origin);
  }
}

/**
 * Add an IP address or prefix to a layer-2 interface. This represents
 * an address of the local radio or modem.
 * @param l2net layer-2 network object
 * @param ip ip address or prefix
 * @return layer2 ip address object, NULL if out of memory
 */
struct oonf_layer2_peer_address *
oonf_layer2_net_add_ip(struct oonf_layer2_net *l2net,
    const struct oonf_layer2_origin *origin, const struct netaddr *ip) {
  struct oonf_layer2_peer_address *l2addr;

  l2addr = oonf_layer2_net_get_ip(l2net, ip);
  if (!l2addr) {
    l2addr = oonf_class_malloc(&_l2net_addr_class);
    if (!l2addr) {
      return NULL;
    }

    /* copy data */
    memcpy(&l2addr->ip, ip, sizeof(*ip));

    /* set back reference */
    l2addr->l2net = l2net;

    /* add to tree */
    l2addr->_node.key = &l2addr->ip;
    avl_insert(&l2net->local_peer_ips, &l2addr->_node);
  }

  l2addr->origin = origin;
  return l2addr;
}

/**
 * Remove a peer IP address from a layer2 network
 * @param ip ip address or prefix
 * @param origin origin of IP address
 * @return 0 if IP was removed, -1 if it was registered to a different origin
 */
int
oonf_layer2_net_remove_ip(
    struct oonf_layer2_peer_address *ip, struct oonf_layer2_origin *origin) {
  if (ip->origin != origin) {
    return -1;
  }

  avl_remove(&ip->l2net->local_peer_ips, &ip->_node);
  oonf_class_free(&_l2net_addr_class, ip);
  return 0;
}

/**
 * Look for the best matching prefix in all layer2 neighbor addresses
 * that contains a specific address
 * @param addr ip address to look for
 * @return layer2 neighbor address object, NULL if no match was found
 */
struct oonf_layer2_neighbor_address *
oonf_layer2_net_get_best_neighbor_match(const struct netaddr *addr) {
  struct oonf_layer2_neighbor_address *best_match, *l2addr;
  struct oonf_layer2_neigh *l2neigh;
  struct oonf_layer2_net *l2net;
  int prefix_length;

  prefix_length = 256;
  best_match = NULL;

  avl_for_each_element(&_oonf_layer2_net_tree, l2net, _node) {
    avl_for_each_element(&l2net->neighbors, l2neigh, _node) {
      avl_for_each_element(&l2neigh->remote_neighbor_ips, l2addr, _node) {
        if (netaddr_is_in_subnet(&l2addr->ip, addr)
            && netaddr_get_prefix_length(&l2addr->ip) < prefix_length) {
          best_match = l2addr;
          prefix_length = netaddr_get_prefix_length(&l2addr->ip);
        }
      }
    }
  }
  return best_match;
}

/**
 * Add a layer-2 neighbor to a addr.
 * @param l2net layer-2 addr object
 * @param neigh mac address of layer-2 neighbor
 * @return layer-2 neighbor object
 */
struct oonf_layer2_neigh *
oonf_layer2_neigh_add(struct oonf_layer2_net *l2net,
    struct netaddr *neigh) {
  struct oonf_layer2_neigh *l2neigh;

  if (netaddr_get_address_family(neigh) != AF_MAC48
      && netaddr_get_address_family(neigh) != AF_EUI64) {
    return NULL;
  }

  l2neigh = oonf_layer2_neigh_get(l2net, neigh);
  if (l2neigh) {
    return l2neigh;
  }

  l2neigh = oonf_class_malloc(&_l2neighbor_class);
  if (!l2neigh) {
    return NULL;
  }

  memcpy(&l2neigh->addr, neigh, sizeof(*neigh));
  l2neigh->_node.key = &l2neigh->addr;
  l2neigh->network = l2net;

  avl_insert(&l2net->neighbors, &l2neigh->_node);

  avl_init(&l2neigh->destinations, avl_comp_netaddr, false);
  avl_init(&l2neigh->remote_neighbor_ips, avl_comp_netaddr, false);

  oonf_class_event(&_l2neighbor_class, l2neigh, OONF_OBJECT_ADDED);

  return l2neigh;
}

/**
 * Remove all data objects of a certain originator from a layer-2 neighbor
 * object.
 * @param l2neigh layer-2 neighbor
 * @param origin originator number
 * @return true if a value was resetted, false otherwise
 */
bool
oonf_layer2_neigh_cleanup(struct oonf_layer2_neigh *l2neigh,
    const struct oonf_layer2_origin *origin) {
  bool changed = false;
  int i;

  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (l2neigh->data[i]._origin == origin) {
      oonf_layer2_reset_value(&l2neigh->data[i]);
      changed = true;
    }
  }
  return changed;
}


/**
 * Remove all information of a certain originator from a layer-2 neighbor
 * object. Remove the object if its empty.
 * @param l2neigh layer-2 neighbor object
 * @param origin originator number
 * @return true if something was change, false otherwise
 */
bool
oonf_layer2_neigh_remove(struct oonf_layer2_neigh *l2neigh,
    const struct oonf_layer2_origin *origin) {
  struct oonf_layer2_destination *l2dst, *l2dst_it;
  struct oonf_layer2_neighbor_address *l2ip, *l2ip_it;

  bool changed = false;

  if (!avl_is_node_added(&l2neigh->_node)) {
    return false;
  }

  avl_for_each_element_safe(&l2neigh->destinations, l2dst, _node, l2dst_it) {
    if (l2dst->origin == origin) {
      oonf_layer2_destination_remove(l2dst);
      changed = true;
    }
  }

  avl_for_each_element_safe(&l2neigh->remote_neighbor_ips, l2ip, _node, l2ip_it) {
    if (oonf_layer2_neigh_remove_ip(l2ip, origin) == 0) {
      changed = true;
    }
  }

  if (oonf_layer2_neigh_cleanup(l2neigh, origin)) {
    changed = true;
  }

  if (changed) {
    oonf_layer2_neigh_commit(l2neigh);
  }
  return changed;
}

/**
 * Commit all changes to a layer-2 neighbor object. This might remove the
 * object from the database if all data has been removed from the object.
 * @param l2neigh layer-2 neighbor object
 * @return true if the object has been removed, false otherwise
 */
bool
oonf_layer2_neigh_commit(struct oonf_layer2_neigh *l2neigh) {
  size_t i;

  if (l2neigh->destinations.count > 0 || l2neigh->remote_neighbor_ips.count > 0) {
    oonf_class_event(&_l2neighbor_class, l2neigh, OONF_OBJECT_CHANGED);
    return false;
  }

  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (oonf_layer2_has_value(&l2neigh->data[i])) {
      oonf_class_event(&_l2neighbor_class, l2neigh, OONF_OBJECT_CHANGED);
      return false;
    }
  }

  _neigh_remove(l2neigh);
  return true;
}

/**
 * Relabel all neighbor data of one origin to another one
 * @param l2neigh layer2 neighbor object
 * @param new_origin new origin
 * @param old_origin old origin to overwrite
 */
void
oonf_layer2_neigh_relabel(struct oonf_layer2_neigh *l2neigh,
    const struct oonf_layer2_origin *new_origin,
    const struct oonf_layer2_origin *old_origin) {
  struct oonf_layer2_neighbor_address *neigh_ip;
  struct oonf_layer2_destination *l2dst;
  size_t i;

  for (i=0; i<OONF_LAYER2_NEIGH_COUNT; i++) {
    if (oonf_layer2_get_origin(&l2neigh->data[i]) == old_origin) {
      oonf_layer2_set_origin(&l2neigh->data[i], new_origin);
    }
  }

  avl_for_each_element(&l2neigh->remote_neighbor_ips, neigh_ip, _node) {
    if (neigh_ip->origin == old_origin) {
      neigh_ip->origin = new_origin;
    }
  }

  avl_for_each_element(&l2neigh->destinations, l2dst, _node) {
    if (l2dst->origin == old_origin) {
      l2dst->origin = new_origin;
    }
  }
}

/**
 * Add an IP address or prefix to a layer-2 interface. This represents
 * an address of the local radio or modem.
 * @param l2net layer-2 network object
 * @param ip ip address or prefix
 * @return layer2 ip address object, NULL if out of memory
 */
struct oonf_layer2_neighbor_address *
oonf_layer2_neigh_add_ip(struct oonf_layer2_neigh *l2neigh,
    const struct oonf_layer2_origin *origin, const struct netaddr *ip) {
  struct oonf_layer2_neighbor_address *l2addr;

  l2addr = oonf_layer2_neigh_get_ip(l2neigh, ip);
  if (!l2addr) {
    l2addr = oonf_class_malloc(&_l2neigh_addr_class);
    if (!l2addr) {
      return NULL;
    }

    /* copy data */
    memcpy(&l2addr->ip, ip, sizeof(*ip));

    /* set back reference */
    l2addr->l2neigh = l2neigh;

    /* add to tree */
    l2addr->_node.key = &l2addr->ip;
    avl_insert(&l2neigh->remote_neighbor_ips, &l2addr->_node);
  }

  l2addr->origin = origin;
  return l2addr;
}

/**
 * Remove a neighbor IP address from a layer2 neighbor
 * @param ip ip address or prefix
 * @param origin origin of IP address
 * @return 0 if IP was removed, -1 if it was registered to a different origin
 */
int
oonf_layer2_neigh_remove_ip(
    struct oonf_layer2_neighbor_address *ip, const struct oonf_layer2_origin *origin) {
  if (ip->origin != origin) {
    return -1;
  }

  avl_remove(&ip->l2neigh->remote_neighbor_ips, &ip->_node);
  oonf_class_free(&_l2neigh_addr_class, ip);
  return 0;
}

/**
 * add a layer2 destination (a MAC address behind a neighbor) to
 * the layer2 database
 * @param l2neigh layer2 neighbor of the destination
 * @param destination destination address
 * @param origin layer2 origin
 * @return layer2 destination, NULL if out of memory
 */
struct oonf_layer2_destination *
oonf_layer2_destination_add(struct oonf_layer2_neigh *l2neigh,
    const struct netaddr *destination,
    const struct oonf_layer2_origin *origin) {
  struct oonf_layer2_destination *l2dst;

  l2dst = oonf_layer2_destination_get(l2neigh, destination);
  if (l2dst) {
    return l2dst;
  }

  l2dst = oonf_class_malloc(&_l2dst_class);
  if (!l2dst) {
    return NULL;
  }

  /* copy data into destination storage */
  memcpy(&l2dst->destination, destination, sizeof(*destination));
  l2dst->origin = origin;

  /* add back-pointer */
  l2dst->neighbor = l2neigh;

  /* add to neighbor tree */
  l2dst->_node.key = &l2dst->destination;
  avl_insert(&l2neigh->destinations, &l2dst->_node);

  oonf_class_event(&_l2dst_class, l2dst, OONF_OBJECT_ADDED);
  return l2dst;
}

/**
 * Remove a layer2 destination
 * @param l2dst layer2 destination
 */
void
oonf_layer2_destination_remove(struct oonf_layer2_destination *l2dst) {
  if (!avl_is_node_added(&l2dst->_node)) {
    return;
  }
  oonf_class_event(&_l2dst_class, l2dst, OONF_OBJECT_REMOVED);

  avl_remove(&l2dst->neighbor->destinations, &l2dst->_node);
  oonf_class_free(&_l2dst_class, l2dst);
}

/**
 * Get neighbor specific data, either from neighbor or from the networks default
 * @param ifname name of interface
 * @param l2neigh_addr neighbor mac address
 * @param idx data index
 * @return pointer to linklayer data, NULL if no value available
 */
const struct oonf_layer2_data *
oonf_layer2_neigh_query(const char *ifname,
    const struct netaddr *l2neigh_addr, enum oonf_layer2_neighbor_index idx) {
  struct oonf_layer2_net *l2net;
  struct oonf_layer2_neigh *l2neigh;
  struct oonf_layer2_data *data;

  /* query layer2 database about neighbor */
  l2net = oonf_layer2_net_get(ifname);
  if (l2net == NULL) {
    return NULL;
  }

  /* look for neighbor specific data */
  l2neigh = oonf_layer2_neigh_get(l2net, l2neigh_addr);
  if (l2neigh != NULL) {
    data = &l2neigh->data[idx];
    if (oonf_layer2_has_value(data)) {
      return data;
    }
  }

  /* look for network specific default */
  data = &l2net->neighdata[idx];
  if (oonf_layer2_has_value(data)) {
    return data;
  }
  return NULL;
}

/**
 * Get neighbor specific data, either from neighbor or from the networks default
 * @param l2neigh pointer to layer2 neighbor
 * @param idx data index
 * @return pointer to linklayer data, NULL if no value available
 */
const struct oonf_layer2_data *
oonf_layer2_neigh_get_value(const struct oonf_layer2_neigh *l2neigh,
    enum oonf_layer2_neighbor_index idx) {
  const struct oonf_layer2_data *data;

  data = &l2neigh->data[idx];
  if (oonf_layer2_has_value(data)) {
    return data;
  }

  /* look for network specific default */
  data = &l2neigh->network->neighdata[idx];
  if (oonf_layer2_has_value(data)) {
    return data;
  }
  return NULL;
}

/**
 * Changes a layer2 data value
 * @param l2data pointer to layer2 data
 * @param origin origin for new data
 * @param value value of new data
 * @return true if value has changed, false otherwise
 */
bool
oonf_layer2_change_value(struct oonf_layer2_data *l2data,
    const struct oonf_layer2_origin *origin, int64_t value) {
  bool changed = true;

  if (oonf_layer2_has_value(l2data)) {
    changed = value != oonf_layer2_get_value(l2data);
  }
  if (changed) {
    oonf_layer2_set_value(l2data, origin, value);
  }
  else {
    l2data->_origin = origin;
  }
  return changed;
}

/**
 * get neighbor metric metadata
 * @param idx neighbor metric index
 * @return metadata object
 */
const struct oonf_layer2_metadata *
oonf_layer2_get_neigh_metadata(enum oonf_layer2_neighbor_index idx) {
  return &_oonf_layer2_metadata_neigh[idx];
}

/**
 * get network metric metadata
 * @param idx network metric index
 * @return metadata object
 */
const struct oonf_layer2_metadata *
oonf_layer2_get_net_metadata(enum oonf_layer2_network_index idx) {
  return &_oonf_layer2_metadata_net[idx];
}

/**
 * get text representation of network type
 * @param type network type
 * @return text representation
 */
const char *
oonf_layer2_get_network_type(enum oonf_layer2_network_type type) {
  return oonf_layer2_network_type[type];
}

/**
 * get tree of layer2 networks
 * @return network tree
 */
struct avl_tree *
oonf_layer2_get_network_tree(void) {
  return &_oonf_layer2_net_tree;
}

/**
 * get tree of layer2 originators
 * @return originator tree
 */
struct avl_tree *
oonf_layer2_get_origin_tree(void) {
  return &_oonf_originator_tree;
}

/**
 * Removes a layer-2 addr object from the database.
 * @param l2net layer-2 addr object
 */
static void
_net_remove(struct oonf_layer2_net *l2net) {
  struct oonf_layer2_neigh *l2neigh, *l2n_it;

  /* free all embedded neighbors */
  avl_for_each_element_safe(&l2net->neighbors, l2neigh, _node, l2n_it) {
    _neigh_remove(l2neigh);
  }

  oonf_class_event(&_l2network_class, l2net, OONF_OBJECT_REMOVED);

  /* remove interface listener */
  os_interface_remove(&l2net->if_listener);

  /* free addr */
  avl_remove(&_oonf_layer2_net_tree, &l2net->_node);
  oonf_class_free(&_l2network_class, l2net);
}

/**
 * Removes a layer-2 neighbor object from the database
 * @param l2neigh layer-2 neighbor object
 */
static void
_neigh_remove(struct oonf_layer2_neigh *l2neigh) {
  struct oonf_layer2_destination *l2dst, *l2dst_it;

  /* free all embedded destinations */
  avl_for_each_element_safe(&l2neigh->destinations, l2dst, _node, l2dst_it) {
    oonf_layer2_destination_remove(l2dst);
  }

  /* inform user that mac entry will be removed */
  oonf_class_event(&_l2neighbor_class, l2neigh, OONF_OBJECT_REMOVED);

  /* free resources for mac entry */
  avl_remove(&l2neigh->network->neighbors, &l2neigh->_node);
  oonf_class_free(&_l2neighbor_class, l2neigh);
}
