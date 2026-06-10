# DNT notifications

DNT supports observability by sending notifications to collection points. OAM can also provide similar information by directing OAM replies to an specific collector point. The main difference is that OAM replies are always generated in a response to an OAM message (like *ping*), while notifications can be generated periodically or triggered by events or trigger messages.

## Notification framework operation

The Notification framework needs a stream named *notification_session* in the configuration file. The notifications are sent according to the pipeline defined by the *notification_session*. While this supports using any actions supported by DNT pipelines, we suggest the following usage scenarios:

* New stream specifically for notifications. In this case a DetNet stream is defined for the notification messages, possibly with replication at the source and elimination at the destination. Note that all intermediate nodes must be aware of this session. In this case path selection and redundancy are handled by the DetNet stream.
* Use a management network. A simpler method is to use the management network for sending the notification messages. In this case all messages are sent out-of-band, in a best effort manner. Sending on multiple paths may still be possible even in this case. Such an example is given in the */getting_started/scenario_notification*: when 2 paths are available through 2 different interfaces, we can send the messages on both. De-duplication is still possible at higher level since each message is identified by a *notif_seq* (and a *notif_fragment*) field. The notification receiver example (in */json_receiver*) shows how to implement the high level elimination.

Notifications can be completely disabled either by not specifying notification stream in configuration file, or via a [telnet command](oam.md) that enables their filtering.

When the notification data is too big for a single packet, the notification module fragments it into multiple packets. Each of these packets have the same *notif_seq*, they carry part of the data in their *notif_msg*, and the fragmentation information is set in *notif_fragment* in the format of "1/3", "2/3" etc.

There is an example [python application and library](json_receiver/README.md) for collecting notifications, assembling the fragments and filtering duplicates.


## Configuration file

In the configuration file the *notification_session* in the *streams* section is a special stream, if it is present, the notifications will be sent according to this pipeline.
The simplest way of sending out notifications is using one or more *udp-out* interfaces, because then the collector application can be based on a simple UDP receiver.

Thus, the minimal configuration changes to enable notifications are the following:

```ini
[interfaces]

notif = udp-out iface=eno1 dstip=10.10.10.2 dstport=6000

[streams]

notification_session = send notif
```

Of course, multiple notification interfaces can be used, using different paths to the destination for reliability.
It is also possible to use the standard DetNet PRF/PEF functionalities for the notification messages as well, but in most cases a simple sequence number based elimination is enough. An example receiver script is available in the */json_receiver* directory.

At the destination the *notif_hostname* and the *notif_seq* sequence number can be used to filter duplicate notification messages.
In some cases (for example in Mininet) multiple nodes have the same hostname, for this case DNT has a command line parameter *-h* which overrides the hostname (which is also used for generating the OAM node id).


## Push notifications

The *push* notifications are triggered by events, and they are sent immediately. For example, when a user connects to DNT via a telnet interface, a push notification is sent indicating the telnet login. The following notification *push* sources are currently defined:

* "delay", type NOTIF_ERROR : a packet arrived later than the delay specified
* "new src", type NOTIF_INFO: an udp-in interface detected an IP address change on its HW interface
* "new dst", type  NOTIF_INFO: an udp-out interface was notified about a new destination IP
* "dnt", type  NOTIF_INFO: dnt startup notification
* "telnet", type NOTIF_INFO: new telnet client connected
* "send", type NOTIF_ERROR or NOTIF_WARNING: detected anomalies about packet sending
* "mask", type NOTIF_INFO: a replication path has been masked
* "pof", type NOTIF_INFO: packet ordering function was reset on timeout
* "seq_rcvy", type NOTIF_INFO: sequence recovery was reset on timeout
* "diagnostic", type NOTIF_WARNING: sequence recovery module diagnostic message
* "transaction", type NOTIF_INFO: a configuration transaction has been committed
* "triggered_receiver", type NOTIF_INFO: trigger push message, MP received a *trigger* message
* "triggered_source", type NOTIF_INFO: trigger push message, MP initiates a *trigger* message (see the *notif_trigger* telnet command)

It is possible to trigger push notifications with a special *trigger* in-band OAM message, which can be initiated with a telnet command. Its main purpose is to have a controlled way of triggering notifications from MPs (almost) synchronously from different nodes even when nodes are not in sync. (Note: *trigger* OAM messages can be treated also as separator between set of data flow packets.) The *trigger* message can be sent by the *notif_trigger* telnet command, which has as parameter the source MP and destination MP, just like a *ping* command. When the command is executed, the source MP triggers a push notification, sending the statistics of the starting MP, the target object statistics and all MP statistics where the target object is common. This typically results in sending pre-object and post-object statistics along with the object stats. The receiver MP of the *trigger* message acts similarly, generating a *triggered_receiver* notification.

When AutoMIP is used on an object, the target objects of the created MIPs are all set to be that object, so their trigger notifications will contain statistics of all the related MIPs. When MIPs are added manually, specifying their target object is optional. When a MIP has no target object, its trigger notification will only contain its own stats.


## Pull notifications

The *pull* notifications are triggered periodically, with a fixed period of 2 seconds. The reporting period is aligned to the even seconds according to the node's clock, therefore when all nodes running DNT are in sync (with PTP), the reporting happens at the same time on all nodes.

On startup, all notification sources in DNT register themselves to the notification module. When reporting, the notification module queries all the registered notification sources, which all provide a json report about their state. The notification module collects these, and runs the *notification_session* pipeline on the resulting packets.

Currently the following pull notification sources are implemented:

* interface: packets sent/received
* parser: per-stream packets received (and packets that did not match any stream)
* MP: state and packet counters
* pipeline object states (seq_gen, seq_recovery, pof, replicate)
* delay statistics per-stream (correctly delayed, delay exceeded)
* sysmon: tc and modem monitoring state


## Filtering of notifications

By default, push notification sources are enabled and pull notifications are disabled. To enable pull notifications, use the *notif_pull enable* [telnet command](oam.md).

Notifications have a *source* and a *level*. The source is a string, identifying the notification source module.

For push notifications the *source* can be "transaction", "dnt", "new src", "telnet", "mask", "triggered_source", "triggered_receiver" etc. For pull notifications the name can be the reporting module's name as described [here](#message-formats).

The notification level can be the following:  ERROR, WARNING, INFO, PULL (currently all pull sources use the PULL level). It is possible to filter sending notifications according to the level with a command line parameter:

```
-n, --notify={LOG|SUBMIT}:LEVEL
                           Available levels: NONE, ERROR, WARNING, INFO,
                           PULL, ALL
```

This means that we can filter sending notifications based on level. The notification collector can also filter based on these levels and the *source* names.

The LOG setting controls which notification messages get logged on the node (by default only WARNING and up). The SUBMIT setting controls which notification messages get sent to the collector via the *notification_session* (by default everything).


## Traffic characteristic collection with notifications

To collect periodic statistics for traffic management, 2 models are supported:

1. based on pull notifications (mainly useful in a synchronized network)
2. based on push notifications triggered by *trigger* messages (does not require synchronization)

Pull notifications can be used in a synchronized network to provide periodic statistics. All pull statistics are reported periodically, with a pre-defined period of 2s (hardcoded currently) at every even second according to the local clock. This means that all nodes will send their stats in sync, if the nodes are synchronized via PTP. These pull notifications contain all the data about the state of DNT.

However, synchronization may not be available/feasible in all cases. In such cases, the *trigger* notification can be used to perform periodic measurements. When a *trigger* message is sent, a local push of *triggered_source* is also performed. Upon reception of the *trigger* message, a push of *triggered_receiver* is performed. These triggered notifications only contain statistics about the MIPs involved in the message exchange, and the other MIPs that have the same target object.

Although these two push messages are not sent at the exact same time, there is only a one-way delay between them so they can be used for traffic measurement etc.

Note that by default the pull notifications are disabled. They can be enabled with the *notif_pull enable* telnet command.


## System monitor

The system monitor is a source for the notification framework. Its main purpose is to monitor critical system components and resources. The following monitoring functions are currently implemented:

* synchronization monitor. Monitors the Linux PTP daemon, the *ptp4l* via the `pmc` interface. Sends immediate push notification when synchronization is lost.

* Linux Traffic Control monitor. Monitors the interface qdisc setting and statistics via the `tc` command. When TAPRIO qdisc is used, the per-queue statistics can be used for monitoring and diagnostic purposes. It returns the root qdisc information, indifferent of qdisc type.

* Modem monitor. Monitors the modem state and signal quality. In case of Quectel modems even more information is provided like QCSQ and serving cell information.

To activate a system monitor component, the telnet interface can be used. With `sysmon add ...` telnet command, we can activate the monitoring of TC interfaces and/or modem statistics.
For example the following commands activate the TC and modem monitor respectively.

```
sysmon add tc eth0

sysmon add modem ttyUSB2
```

The synchronization monitor is started automatically if `delay` is enabled in the configuration file.


## Related telnet commands

The pull messages can be enabled/disabled from the [telnet interface](oam.md).

* notif_pull enable/disable - Enable or disable pull notifications

* notif_trigger `source MIP` `target MIP` level [options] - send trigger message, and trigger local push notification
  * source MIP is the starting point of the trigger message. It is usually the pre-replication MIP.
  * target MIP is the target of the trigger message. It is usually the post-elimination MIP.
  * level is the trigger message level
  * valid options are the -n, -i and -t - similar to the ping options: n=count of packets, -i is the interval_ms, and -t is the TTL

* sysmon `command` `type` `target` [period_ms] - add/remove system monitoring.
    Thee `command` is either `add` or `rem` - for remove.
    The `type` can be: `tc`, `modem`. The `target` is specific to the command, as described below.
    * tc - monitor the Linux Advanced Routing and Traffic Control, via the `tc` command. For `tc` type, the `target` is and interface name to be monitored. The system monitor will periodically query the root qdisc stats, and report the received json as-is.
    * modem - monitor the attached modem statistics. The `target` is the name of the modem's TTY device name. For example, an attached Quectel USB modem will show 4 new USB TTY devices, for example /dev/ttyUSB0, /dev/ttyUSB1, /dev/ttyUSB2, and /dev/ttyUSB3. For monitor, the AT command TTY device must be specified, usually ttyUSB2. The `target` is the device name, without the "/dev/" prefix.



## Message formats

Each component of DNT reports its state in the notification message in a specific structure
First, the object-specific JSON messages are described, then the notification messages themselves.

### Object report JSONs

* MP report:

```
    "mp_name": {
        "data_octets": 0,
        "data_packets": 0,
        "mask_signal_state": "masked"/"unmasked",   - used by mask signaling
        "name": "mip_name",                         - the name of the sender
        "oam_recv": 0,
        "oam_send": 0,
        "type": "mep_state"                         - type
    }
```
  The MEP/MIP has separate counters for OAM and regular user traffic received within a stream. Since OAM packets share fate with the normal traffic, their identification at interface/parser level is not possible: those statistics include both normal and OAM traffic.

* Sequence generator report:

```
    "name": {
        "name": "name",                         - name of the object
        "type": "seqgen",
        "use_init_flag": true/false,
        "use_reset_flag": true/false
        }
```

* Sequence recovery report:

```
    "name": {
        "discarded_packets": 0,
        "history": "000000000000000",           - only in debug mode
        "history_length": 15,                   - configured parameters
        "latent_error_paths": 2,
        "latent_error_resets": 4,
        "latent_errors": 0,
        "name": "srcvy1",
        "passed_packets": 0,
        "recovery_algorithm": "vector",
        "recovery_seq_num": 65535,
        "reset_msec": 2000,
        "seq_recovery_resets": 2,
        "type": "seqrec",
        "use_init_flag": true/false,
        "use_reset_flag": true/false
        }
```

*The history vector is only sent in debug mode.*

* Replication report:

```
    "prf": {
        "name": "prf",
        "octets_passed": 0,
        "packets_passed": 0,
        "pipelines": [                      - the replication pipelines
            {"action_count": 7, "mask_state": "masked", "name": "to_nni0"},
            {"action_count": 7, "mask_state": "unmasked", "name": "to_nni1"}
            ],
        "type": "replicate"
        }
```

* Delay report:

```
    "delay": {
        "stream": {"delay_exceeded_packets": 0, "delayed_packets": 0}
        }
```

The *delayed_packets* are the packets delayed in total by the delay module.
The *delay_exceeded_packets* are the packets which arrived so late: their delay is already exceeded at the moment of reception. This indicates that either the delay in the network increased significantly, or the *delay_ms* parameter in the configuration is too low.

* Interface report:

```
    "interface": {
        "recv_octets": 0,
        "recv_packets": 0,
        "send_octets": 0,
        "send_packets": 0
    }
```

* Interface parser report:

```
    "interface parser": {
        "no match octets": 0,            - packets with no match in the stream list
        "no match packets": 0,
        "stream1 octets": 0,             - per stream 2 values
        "stream1 packets": 0,
        "stream2 octets": 0,
        "stream2 packets": 0}
```

Increasing number of "no match packets" indicates that traffic other than TSN/DetNet reaches the DNT.
Usually we use TC filters or OVS to decouple the background traffic. However, in some cases traffic like ARP can still reach the DNT. Excessive increase of "no match packets" indicates configuration error or misconfiguration of streams.

* TC report:

```
    "tc_interface": {
        json from TC output, specific to the TC qdisc used. The "tc -s -j qdisc show dev `interface` handle 0" command output is added here.
    }
```

* Modem report:
The modem report consists of 4 JSON values, as responses for the corresponding AT commands.
An example is:

```
"modem_ttyUSB2": {          - modem notification for TTYUSB2
    "CEREG": {              - Registration status, according to the standard AT+CEREG? command.
        "n": 0,
        "stat": 1
    },
    "CSQ": {                - Signal Quality Report, result of AT+CSQ commaand.
        "ber": 99,
        "rssi": 20
    },
    "qcsq":                 - Query report of signal strength, result of AT+QCSQ command.
        "'LTE',-72,-106,-3,-17",   - the report is service mode specific. In the example, an LTE mode response is shown.

    "servingcell":          - Query report of the serving cell information, result of the AT+QENG="servingcell" command.
    For the exact format of the response, check the modem's AT command reference.
        '"servingcell","NOCONN","LTE","FDD",216,30,16506,341,1644,3,3,3,2EE7,-107,-18,-71,8,0,-,16'
    }
```

Note that the *CEREG* and *CSQ* reports are results of standard AT commands, while the *qcsq* and *servingcell* information are Quectel specific. For modems of other vendors, different commands may be necessary.

### Notification message JSONs

The notification system uses message fragmentation, which ensures that each fragment fits into an UDP packet.
To identify the fragments, two fields are used: the *notif_seq* and the *notif_fragment*. The *notif_seq* identifies the individual messages, which may be longer than 1200 byte. The *notif_fragment* tells which fragment this is, and how many fragments are in total.

```json
{
    "notif_msg": "the actual json content of the message, fragmented in 1200 byte chunks",
    "notif_hostname": "hostname",
    "notif_seq": 0,
    "notif_fragment": "1/2",
    "notif_tstamp": 1743628439.8999681
}
```

The *notif_message* holds the notification JSON message chunks, and by concatenating the fragments we restore the original JSON message.

In the following, we present the full message formats, without the *notif_fragment* field. This field is not needed after reassembly, so the provided *NotificationReceiver* Python class removes it.

##### Pull notifications

* Pull notification

```json
{
    "notif_msg": "{
              source_1: object report 1
              source_2: object report 2
              source_n: object report n
    }",
    "notif_hostname": "hostname",
    "notif_seq": 0,
    "notif_tstamp": 1743628439.8999681
}
```
Where source_1...n are the registered pull notification sources, that can be the ones described in the previous section.

##### Push notifications

* Configuration file commit notification:

```json
{
    "notif_msg": "{
        \"push_level\": \"INFO\",
        \"transaction\": {\"committed\": \"notification/notification-detnet.ini\"}
    }",
    "notif_hostname": "hostname",
    "notif_seq": 0,
    "notif_tstamp": 1743628439.8999681
}
```

Sent whenever DNT commits a valid configuration.

* Startup notification:

```json
{
    "notif_hostname": "hostname",
    "notif_seq": 1,
    "notif_tstamp": 1743628439.9162316,
    "notif_msg": "{
        \"push_level\": \"INFO\",
        \"dnt\": {\"status\": \"startup completed\"}
    }"
}
```
Sent when DNT is ready to process the incoming traffic.

* IP change notification:

```
    "notif_msg": {
        "push_level": "INFO",
        "new src": {"interface": "if3", "ip": "172.31.32.90", "port": 6635}
    }
    "notif_hostname": "hostname",
    "notif_seq": 2,
    "notif_tstamp": 1743628440.05333
```
Message sent whenever an IP change is detected at an interface.

* Telnet login notification:


```
    "notif_msg": {
        "push_level": "INFO",
        "telnet": {"ip": "127.0.0.1", "login": "conn 11", "port": 48194}
    }
    "notif_hostname": "hostname",
    "notif_seq": 3,
    "notif_tstamp": 1743628440.8810227
```
Message sent whenever there is a new login at the telnet interface.

* Mask notification:

```
    "notif_msg": {
        "push_level": "INFO",
        "mask": {"source_pipeline": "to_nni0", "status": "masked"}
    }
    "notif_hostname": "hostname",
    "notif_seq": 4,
    "notif_tstamp": 1743628441.0816114
```
Notifies a mask/unmask operation performed.

* Triggered Source notification:

Trigger message triggers 2 types of notifications: a *triggered_source* at the source and a *triggered_receiver* at the destination. Both contain similar information: MEP state of the source MEP, target object statistics (if the source MEP has a target object) and stats of all MIPs that have the same target object. This means that typically when 2 paths are used 3 MEP/MIP reports and an object report is sent.
When AutoMIP is used, the target is automatically filled so all MIP reports are included. When adding MIP manually, when a target is not specified it will not be included in the notification report.

```
    "notif_msg": {
        "push_level": "INFO",
        "triggered_source": {
            "level": 5,
            "mep": [
                {MEP state 1},
                {MEP state 2},
                {MEP state ...},
                {Target object state},
                ],
            "node_id": 1,
            "seq": 0,
            "session": 1,
            "source": "source MIP",
            "stream": "stream",
            "target": "target MIP"
        }
    }
    "notif_hostname": "hostname",
    "notif_seq": 5,
    "notif_tstamp": 1743628441.283137
```

* Triggered Receiver notification:

```
    "notif_msg": {
        "push_level": "INFO",
        "triggered_receiver": {
            "level": 5,
            "mep": [
                {MEP state 1},
                {MEP state 2},
                {MEP state ...},
                {Target object state},
                ],
            "node_id": 1,
            "seq": 0,
            "session": 1,
            "source": "source MIP",
            "stream": "stream",
            "target": "target MIP"
        }
    }
    "notif_hostname": "hostname",
    "notif_seq": 5,
    "notif_tstamp": 1743628441.283137
```

