
/*
 * The olsr.org Optimized Link-State Routing daemon(olsrd)
 * Copyright (c) 2004-2011, the olsr.org team - see HISTORY file
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

#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>

#include "common/autobuf.h"
#include "common/avl.h"
#include "common/list.h"
#include "olsr_cfg.h"
#include "olsr_logging.h"
#include "olsr_memcookie.h"
#include "olsr_socket.h"
#include "olsr_timer.h"
#include "os_net.h"
#include "olsr.h"
#include "olsr_stream_socket.h"

struct list_entity olsr_stream_head;

/* server socket */
static struct olsr_memcookie_info *connection_cookie;
static struct olsr_timer_info *connection_timeout;

/* remember if initialized or not */
OLSR_SUBSYSTEM_STATE(olsr_stream_state);

static int _apply_managed_socket(struct olsr_stream_managed *managed,
    struct olsr_stream_socket *stream, struct netaddr *bindto, uint16_t port);
static void _parse_request(int fd, void *data, unsigned int flags);
static struct olsr_stream_session *_create_session(
    struct olsr_stream_socket *comport, int sock, struct netaddr *remote_addr);
static void _parse_connection(int fd, void *data,
        enum olsr_sockethandler_flags flags);

static void _timeout_handler(void *);

/**
 * Initialize the stream socket handlers
 * @return -1 if an error happened, 0 otherwise.
 */
int
olsr_stream_init(void) {
  if (olsr_subsystem_init(&olsr_stream_state))
    return 0;

  connection_cookie = olsr_memcookie_add("stream socket connections",
      sizeof(struct olsr_stream_session));
  if (connection_cookie == NULL) {
    OLSR_WARN_OOM(LOG_SOCKET_STREAM);
    olsr_stream_state--;
    return -1;
  }

  connection_timeout = olsr_timer_add("stream socket timout",
      &_timeout_handler, false);
  if (connection_timeout == NULL) {
    OLSR_WARN_OOM(LOG_SOCKET_STREAM);
    olsr_memcookie_remove(connection_cookie);
    olsr_stream_state--;
    return -1;
  }

  list_init_head(&olsr_stream_head);
  return 0;
}

/**
 * Cleanup all resources allocated be stream socket handlers
 */
void
olsr_stream_cleanup(void) {
  struct olsr_stream_socket *comport;

  if (olsr_subsystem_cleanup(&olsr_stream_state))
    return;

  while (!list_is_empty(&olsr_stream_head)) {
    comport = list_first_element(&olsr_stream_head, comport, node);

    olsr_stream_remove(comport);
  }

  olsr_memcookie_remove(connection_cookie);
  olsr_timer_remove(connection_timeout);
}

/**
 * Flush all data in outgoing buffer of a stream socket
 * @param con pointer to stream socket
 */
void
olsr_stream_flush(struct olsr_stream_session *con) {
  olsr_socket_enable(con->scheduler_entry, OLSR_SOCKET_WRITE);
}

/**
 * Add a new stream socket to the scheduler
 * @param comport pointer to uninitialized stream socket struct
 * @param local pointer to local ip/port of socket
 * @return -1 if an error happened, 0 otherwise
 */
int
olsr_stream_add(struct olsr_stream_socket *comport,
    union netaddr_socket *local) {
  int s = -1;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  memset(comport, 0, sizeof(*comport));

  /* Init socket */
  s = os_net_getsocket(local, OS_SOCKET_TCP, 0, LOG_SOCKET_STREAM);
  if (s < 0) {
    goto add_stream_error;
  }

  /* show that we are willing to listen */
  if (listen(s, 1) == -1) {
    OLSR_WARN(LOG_SOCKET_STREAM, "tcp socket listen failed for %s: %s (%d)\n",
        netaddr_socket_to_string(&buf, local), strerror(errno), errno);
    goto add_stream_error;
  }

  if ((comport->scheduler_entry = olsr_socket_add(s,
      _parse_request, comport, OLSR_SOCKET_READ)) == NULL) {
    OLSR_WARN(LOG_SOCKET_STREAM, "tcp socket hookup to scheduler failed for %s\n",
        netaddr_socket_to_string(&buf, local));
    goto add_stream_error;
  }
  memcpy(&comport->local_socket, local, sizeof(comport->local_socket));

  if (comport->config.memcookie == NULL) {
    comport->config.memcookie = connection_cookie;
  }
  if (comport->config.allowed_sessions == 0) {
    comport->config.allowed_sessions = 10;
  }
  if (comport->config.maximum_input_buffer == 0) {
    comport->config.maximum_input_buffer = 65536;
  }

  list_init_head(&comport->session);
  list_add_tail(&olsr_stream_head, &comport->node);

  return 0;

add_stream_error:
  if (s != -1) {
    os_close(s);
  }
  if (comport->scheduler_entry) {
    olsr_socket_remove(comport->scheduler_entry);
  }
  return -1;
}

void
olsr_stream_remove(struct olsr_stream_socket *comport) {
  struct olsr_stream_session *session;

  if (list_node_added(&comport->node)) {
    comport = list_first_element(&olsr_stream_head, comport, node);
    while (!list_is_empty(&comport->session)) {
      session = list_first_element(&comport->session, session, node);
      olsr_stream_close(session);
    }

    list_remove(&comport->node);

    if (comport->scheduler_entry) {
      os_close(comport->scheduler_entry->fd);
      olsr_socket_remove(comport->scheduler_entry);
    }
  }
}

struct olsr_stream_session *
olsr_stream_connect_to(struct olsr_stream_socket *comport,
    union netaddr_socket *remote) {
  struct olsr_stream_session *session;
  struct netaddr remote_addr;
  bool wait_for_connect = false;
  int s;
#if !defined REMOVE_LOG_WARN
  struct netaddr_str buf;
#endif

  s = os_net_getsocket(remote, OS_SOCKET_TCP, 0, LOG_SOCKET_STREAM);
  if (s < 0) {
    return NULL;
  }

  if (connect(s, &remote->std, sizeof(*remote))) {
    if (errno != EINPROGRESS) {
      OLSR_WARN(LOG_SOCKET_STREAM, "Cannot connect outgoing tcp connection to %s: %s (%d)",
          netaddr_socket_to_string(&buf, remote), strerror(errno), errno);
      goto connect_to_error;
    }
    wait_for_connect = true;
  }

  netaddr_from_socket(&remote_addr, remote);
  session = _create_session(comport, s, &remote_addr);
  if (session) {
    session->wait_for_connect = wait_for_connect;
    return session;
  }

  /* fall through */
connect_to_error:
  if (s) {
    os_close(s);
  }
  return NULL;
}

void
olsr_stream_set_timeout(struct olsr_stream_session *con, uint32_t timeout) {
  olsr_timer_set(&con->timeout, timeout, 0, con, connection_timeout);
}

void
olsr_stream_close(struct olsr_stream_session *session) {
  if (session->comport->config.cleanup) {
    session->comport->config.cleanup(session);
  }

  session->comport->config.allowed_sessions++;
  list_remove(&session->node);

  os_close(session->scheduler_entry->fd);
  olsr_socket_remove(session->scheduler_entry);

  abuf_free(&session->in);
  abuf_free(&session->out);

  olsr_memcookie_free(session->comport->config.memcookie, session);
}

void
olsr_stream_add_managed(struct olsr_stream_managed *managed) {
  memset(managed, 0, sizeof(*managed));
  managed->config.allowed_sessions = 10;
  managed->config.maximum_input_buffer = 65536;
  managed->config.session_timeout = 120000;
}

int
olsr_stream_apply_managed(struct olsr_stream_managed *managed,
    struct olsr_stream_managed_config *config) {
  olsr_acl_copy(&managed->acl, &config->acl);

  if (config_global.ipv4) {
    if (_apply_managed_socket(managed,
        &managed->socket_v4, &config->bindto_v4, config->port)) {
      return -1;
    }
  }
  else {
    olsr_stream_remove(&managed->socket_v4);
  }

  if (config_global.ipv6) {
    if (_apply_managed_socket(managed,
        &managed->socket_v6, &config->bindto_v6, config->port)) {
      return -1;
    }
  }
  else {
    olsr_stream_remove(&managed->socket_v6);
  }
  return 0;
}

void
olsr_stream_remove_managed(struct olsr_stream_managed *managed) {
  olsr_stream_remove(&managed->socket_v4);
  olsr_stream_remove(&managed->socket_v6);

  olsr_acl_remove(&managed->acl);
}

static int
_apply_managed_socket(struct olsr_stream_managed *managed,
    struct olsr_stream_socket *stream,
    struct netaddr *bindto, uint16_t port) {
  union netaddr_socket sock;
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  if (netaddr_socket_init(&sock, bindto, port)) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot create managed socket address: %s/%u",
        netaddr_to_string(&buf, bindto), port);
    return -1;
  }

  if (memcmp(&sock, &stream->local_socket, sizeof(sock)) == 0) {
    /* nothing changed */
    return 0;
  }

  olsr_stream_remove(stream);
  if (olsr_stream_add(stream, &sock)) {
    return -1;
  }

  /* copy configuration */
  memcpy(&stream->config, &managed->config, sizeof(stream->config));
  if (stream->config.memcookie == NULL) {
    stream->config.memcookie = connection_cookie;
  }
  return 0;
}

static void
_parse_request(int fd, void *data, unsigned int flags) {
  struct olsr_stream_socket *comport;
  union netaddr_socket remote_socket;
  struct netaddr remote_addr;
  socklen_t addrlen;
  int sock;
#if !defined(REMOVE_LOG_DEBUG)
      struct netaddr_str buf1, buf2;
#endif

  if ((flags & OLSR_SOCKET_READ) == 0) {
    return;
  }

  comport = data;

  addrlen = sizeof(remote_socket);
  sock = accept(fd, &remote_socket.std, &addrlen);
  if (sock < 0) {
    OLSR_WARN(LOG_SOCKET_STREAM, "accept() call returned error: %s (%d)", strerror(errno), errno);
    return;
  }

  netaddr_from_socket(&remote_addr, &remote_socket);
  if (comport->config.acl) {
    if (!olsr_acl_check_accept(comport->config.acl, &remote_addr)) {
      OLSR_DEBUG(LOG_SOCKET_STREAM, "Access from %s to socket %s blocked because of ACL",
          netaddr_to_string(&buf1, &remote_addr),
          netaddr_socket_to_string(&buf2, &comport->local_socket));
      close(sock);
      return;
    }
  }
  _create_session(comport, sock, &remote_addr);
}

static struct olsr_stream_session *
_create_session(struct olsr_stream_socket *comport,
    int sock, struct netaddr *remote_addr) {
  struct olsr_stream_session *session;
#if !defined REMOVE_LOG_DEBUG
  struct netaddr_str buf;
#endif

  /* put socket into non-blocking mode */
  if (os_net_set_nonblocking(sock)) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot read comport socket status: %s (%d)",
        strerror(errno), errno);
    return NULL;
  }

  session = olsr_memcookie_malloc(comport->config.memcookie);
  if (session == NULL) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot allocate memory for comport session");
    goto parse_request_error;
  }

  if (abuf_init(&session->in, 1024)) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot allocate memory for comport session");
    goto parse_request_error;
  }
  if (abuf_init(&session->out, 0)) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot allocate memory for comport session");
    goto parse_request_error;
  }

  if ((session->scheduler_entry = olsr_socket_add(sock,
      &_parse_connection, session,
      OLSR_SOCKET_READ | OLSR_SOCKET_WRITE)) == NULL) {
    OLSR_WARN(LOG_SOCKET_STREAM, "Cannot hook incoming session into scheduler");
    goto parse_request_error;
  }

  session->send_first = comport->config.send_first;
  session->comport = comport;

  session->remote_address = *remote_addr;

  if (comport->config.allowed_sessions-- > 0) {
    /* create active session */
    session->state = STREAM_SESSION_ACTIVE;
  } else {
    /* too many sessions */
    if (comport->config.create_error) {
      comport->config.create_error(session, STREAM_SERVICE_UNAVAILABLE);
    }
    session->state = STREAM_SESSION_SEND_AND_QUIT;
  }

  if (comport->config.session_timeout) {
    session->timeout = olsr_timer_start(
        comport->config.session_timeout, 0, session, connection_timeout);
  }

  if (comport->config.init) {
    if (comport->config.init(session)) {
      goto parse_request_error;
    }
  }

  OLSR_DEBUG(LOG_SOCKET_STREAM, "Got connection through socket %d with %s.\n",
      sock, netaddr_to_string(&buf, remote_addr));

  list_add_tail(&comport->session, &session->node);
  return session;

parse_request_error:
  abuf_free(&session->in);
  abuf_free(&session->out);
  olsr_memcookie_free(comport->config.memcookie, session);

  return NULL;
}

static void
_timeout_handler(void *data) {
  struct olsr_stream_session *session = data;
  olsr_stream_close(session);
}

static void
_parse_connection(int fd, void *data,
    enum olsr_sockethandler_flags flags) {
  struct olsr_stream_session *session;
  struct olsr_stream_socket *comport;
  int len;
  char buffer[1024];
#if !defined(REMOVE_LOG_WARN)
  struct netaddr_str buf;
#endif

  session = data;
  comport = session->comport;

  OLSR_DEBUG(LOG_SOCKET_STREAM, "Parsing connection of socket %d\n", fd);

  if (session->wait_for_connect) {
    if (flags & OLSR_SOCKET_WRITE) {
      int value;
      socklen_t value_len;

      value_len = sizeof(value);

      if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &value, &value_len)) {
        OLSR_WARN(LOG_SOCKET_STREAM, "getsockopt failed: %s (%d)",
            strerror(errno), errno);
        session->state = STREAM_SESSION_CLEANUP;
      }
      else if (value != 0) {
        OLSR_WARN(LOG_SOCKET_STREAM, "Connection to %s failed: %s (%d)",
            netaddr_to_string(&buf, &session->remote_address), strerror(value), value);
        session->state = STREAM_SESSION_CLEANUP;
      }
      else {
        session->wait_for_connect = false;
      }
    }
  }

  if (session->wait_for_connect) {
    return;
  }

  /* read data if necessary */
  if (session->state == STREAM_SESSION_ACTIVE && (flags & OLSR_SOCKET_READ)
      != 0) {
    len = recv(fd, buffer, sizeof(buffer), 0);
    if (len > 0) {
      OLSR_DEBUG(LOG_SOCKET_STREAM, "  recv returned %d\n", len);
      if (abuf_memcpy(&session->in, buffer, len)) {
        /* out of memory */
        OLSR_WARN(LOG_SOCKET_STREAM, "Out of memory for comport session input buffer");
        session->state = STREAM_SESSION_CLEANUP;
      } else if (session->in.len > comport->config.maximum_input_buffer) {
        /* input buffer overflow */
        if (comport->config.create_error) {
          comport->config.create_error(session, STREAM_REQUEST_TOO_LARGE);
        }
        session->state = STREAM_SESSION_SEND_AND_QUIT;
      } else {
        /* got new input block, reset timeout */
        olsr_stream_set_timeout(session, comport->config.session_timeout);
      }
    } else if (len < 0 && errno != EINTR && errno != EAGAIN && errno
        != EWOULDBLOCK) {
      /* error during read */
      OLSR_WARN(LOG_SOCKET_STREAM, "Error while reading from communication stream with %s: %s (%d)\n",
          netaddr_to_string(&buf, &session->remote_address), strerror(errno), errno);
      session->state = STREAM_SESSION_CLEANUP;
    } else if (len == 0) {
      /* external socket closed */
      session->state = STREAM_SESSION_SEND_AND_QUIT;
    }
  }

  if (session->state == STREAM_SESSION_ACTIVE && comport->config.receive_data != NULL
      && (session->in.len > 0 || session->send_first)) {
    session->state = comport->config.receive_data(session);
    session->send_first = false;
  }

  /* send data if necessary */
  if (session->state != STREAM_SESSION_CLEANUP && session->out.len > 0) {
    if (flags & OLSR_SOCKET_WRITE) {
      len = send(fd, session->out.buf, session->out.len, 0);

      if (len > 0) {
        OLSR_DEBUG(LOG_SOCKET_STREAM, "  send returned %d\n", len);
        abuf_pull(&session->out, len);
        olsr_stream_set_timeout(session, comport->config.session_timeout);
      } else if (len < 0 && errno != EINTR && errno != EAGAIN && errno
          != EWOULDBLOCK) {
        OLSR_WARN(LOG_SOCKET_STREAM, "Error while writing to communication stream with %s: %s (%d)\n",
            netaddr_to_string(&buf, &session->remote_address), strerror(errno), errno);
        session->state = STREAM_SESSION_CLEANUP;
      }
    } else {
      OLSR_DEBUG(LOG_SOCKET_STREAM, "  activating output in scheduler\n");
      olsr_socket_enable(session->scheduler_entry, OLSR_SOCKET_WRITE);
    }
  }

  if (session->out.len == 0) {
    /* nothing to send anymore */
    OLSR_DEBUG(LOG_SOCKET_STREAM, "  deactivating output in scheduler\n");
    olsr_socket_disable(session->scheduler_entry, OLSR_SOCKET_WRITE);
    if (session->state == STREAM_SESSION_SEND_AND_QUIT) {
      session->state = STREAM_SESSION_CLEANUP;
    }
  }

  /* end of connection ? */
  if (session->state == STREAM_SESSION_CLEANUP) {
    OLSR_DEBUG(LOG_SOCKET_STREAM, "  cleanup\n");

    /* clean up connection by calling cleanup directly */
    olsr_timer_stop(session->timeout);
    session->timeout = NULL;
    olsr_stream_close(session);
  }
  return;
}
