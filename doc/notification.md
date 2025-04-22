# R2DTWO notifications

R2DTWO supports observability by sending notifications to collection points. OAM can also provide similar information by directing OAM replies to an specific collector point. The main difference is that OAM replies are always generated in a response to an OAM message (like *ping*), while notifications can be generated periodically or triggered by events or trigger messages.

## Notification framework operation

The Notification framework is enabled when a session named *notification_session* is created in the configuration file. The notifications are sent according to the pipeline defined by the *notification_session*. While this makes possible any actions supported by R2DTWO, we suggest the following usage scenarios:

* New stream for notifications (with designated replication). In this case a DetNet stream is defined for the notification messages, with replication at the source and elimination at the destination. Note that all intermediate nodes must be aware of this session. In this case path selection, redundancy is all handled by the DetNet stream.
* Use the management network. A simpler method is to use the management network to send the notification messages. In this case all messages are sent out-of-band, in a best effort manner. Sending on multiple paths may still be possible even in this case. Such an example is given in the */getting_started/scenario_notification*: when 2 paths are available through 2 interfaces, we can send the messages on both. De-duplication is still possible at higher level since each message is identified by a *notif_seq* field. The notification receiver example (in */json_receiver*) gives an example for the high level elimination.

Notifications can be completely disabled either by not specifying notification stream in configuration file, telnet command and filtering.

There are 2 notification types:

* push
* pull

## Push notifications

The *push* notifications are triggered by events, and they are sent immediately. For example, when a user connects to R2DTWO via a telnet interface, a push notification is sent indicating a telnet login. The following notification *push* sources are currently defined:

* "delay", type NOTIF_ERROR : a packet arrived later than the delay specified
* "new src", type NOTIF_INFO: a new notification source has been registered
* "new dst", type  NOTIF_INFO:  a new destination IP address has been set
* "r2dtwo", type  NOTIF_INFO: r2dtwo start notification
* "telnet", type NOTIF_INFO: new telnet client
* "send", type NOTIF_ERROR or NOTIF_WARNING: packet sending related information, error or warning
* "mask", type NOTIF_INFO: a replication path has been masked
* "pof", type NOTIF_INFO: packet ordering function has encountered an error
* "seq_rcvy", type NOTIF_INFO: sequence recovery event
* "diagnostic", type NOTIF_WARNING: diagnostic module message
* "transaction", type NOTIF_INFO: ???_???
* "triggered_receiver", type NOTIF_INFO: trigger push message, MP received a trigger message
* "triggered_source", type NOTIF_INFO: trigger push message, MP initiates a trigger message

It is possible to trigger push notifications with a special *trigger* OAM message. The *trigger* OAM message can be generated from telnet command. Its main purpose is to have a controlled way of triggering notifications (almost) synchronously from different nodes even when nodes are not in sync. (Note: *trigger* OAM messages can be treated also as separator between set of data flow packets.) The *trigger* message can be triggered by the *notif_trigger* telnet command, which has as parameter the source MP and destination MP, just like a *ping* command. When the command is executed, the source MP triggers a push notification, sending the starting MP statistics, the target object statistics and all MP statistics where the target object is common. This typically results in sending pre-object and post-object statistics along with the object stats.
When AutoMIP is used, the target objects are automatically filled, so notifications will contain all related information. However, when MIPs are added manually, specifying target MIP is optional. When no target object is specified, the notification will only contain the source MP stats.


## Pull notifications

The *pull* notifications are triggered periodically, with a fixed period currently set to 2 seconds. The reporting period is aligned to the 2 second reporting from epoch so when all devices running R2DTWO are in sync, the reporting periods will also be in sync. On startup, all notification sources register themselves to the notification module. When reporting, all notification sources are queried and all provide a json report which will be collected and sent to the notification destination. So in each 2 seconds all registered pull notification sources will report their information.

Currently the following pull notification sources are implemented:

* interface, identifier: interface name
* MIP, identifier: name
* parser, identifier name+" parser"
* pof, identifier: name
* replicate, identifier: name
* sequence generator, identifier: name
* sequence recovery, identifier: name
* delay, identifier "delay"
* sysmon TC, identifier: "tc_"+target interface
* sysmon modem, identifier: "modem_"+target interface

Each *pull* notification source provides a specific json message, which will be collected in notification pull messages. One notification pull message may contain json from one or more notification sources, depending on their specific json length.

## Traffic characteristic collection with notifications

To collect periodic statistics for traffic management, 2 models are supported:

1. based on pull notifications (in a synchronized network)
2. based on push notifications triggered by *trigger* messages (does not require synchronization)

Pull notifications can be used in a synchronized network to provide periodic statistics. All pull statistics are reported periodically, with a pre-defined period of 2s (hardcoded currently) starting at 0 second border. This means that all nodes will send their stats in sync, periodically.

However, synchronization may not be available/feasible in all cases. In such cases, the *trigger* notification can be used to perform periodic measurements. When a *trigger* message is sent, a local push is also performed. Upon reception of the *trigger* message, a push is also performed at the destination. Although the push messages are not completely in sync, only a one-way delay is between them so they can be used for traffic measurement etc.

Note that by default the pull notifications are disabled. They can be enabled using the 'notif_pull enable' telnet command.


## Filtering of notifications

By default, push notification sources are enabled and pull notifications are disabled. To enable pull notifications, the *notif_pull enable* command can be used.

Notifications have a *source* and a *level*. The source is a string, describing the notification source.

For push notifications the *source* can be "transaction", "r2dtwo", "new src", "telnet", "mask", "triggered_source" and "triggered_receiver". For pull notifications the name can be the reporting module name as described [here](#Message formats)

The notification level can be the following:  NONE, ERROR, WARNING, INFO, PULL, ALL. It is possible to filter sending notifications according to the level, from command line parameter:

```
-n, --notify={LOG|SUBMIT}:LEVEL
                           Available levels: NONE, ERROR, WARNING, INFO,
                           PULL, ALL
```

This means that we can filter sending notifications based on level. However, the *source* field is present in message thus at the destination is possible to implement any filtering desired.


## Configuration file

In configuration file the *notification_session* keyword is used under the [streams]. If the *notification_session* stream is present, the notifications will be sent according to this pipeline. An *udp-out* interface is also needed to send the packets.
It is recommended to use one or more specific udp-out interface(s) for notifications.
Thus, the minimal configuration changes to enable notifications are the following:

```
[interfaces]
...
notif = udp-out iface=eno1 dstip=10.10.10.2 dstport=6000

[streams]
...
notification_session = send notif
```
Of course, multiple notification interfaces can be used, using different paths to the destination. At the destination the hostname and notification message sequence number can be used to drop notification message duplicates.
It is also possible to use PRF/PEF for the notification messages as well, but in most cases a simple sequence number based elimination is enough. An example receiver script is available in /json_receiver/json_udp_receiver.py file.

In some cases (for example in mininet) the same hostname is used for multiple nodes. In this case the R2DTWO has a command line parameter *-h* which specifies the hostname. In this case the given hostname will be used instead of the system reported hostname.

## Related telnet commands

The pull messages can be enabled/disabled from telnet command.
* notif_pull enable/disable - Enable or disable pull notification_source
* notif_trigger `source MIP` `target MIP` level [options] - send trigger message, and trigger local push notification
  * source MIP is the starting point of the trigger message. It is usually the pre-replication MIP.
  * target MIP is the target of the trigger message. It is usually the post-elimination MIP.
  * level is the trigger message level
  * valid options are the -n, -i and -t - similar to the ping options: n=count of packets, -i is the interval_ms, and -t is the TTL


## Message formats

Each notification message contains a notification-specific structure, and contains the reports sent by the different objects.
First, the object-specific JSON messages are described, then the notification messages themselves.

### Object report JSONs

* MIP report:

```
    "mip_name": {
        "mask_signal_state": masked/unmasked,   - used by mask signaling
        "name": "mip_name",                     - the name of the sender
        "oam_octets_passed": 0,
        "oam_packets_passed": 0,
        "octets_passed": 0,
        "packets_passed": 0,
        "type": "mep_state"                     - type
    }
```
  The MEP/MIP differentiates between OAM and TSN/DetNet traffic within a stream. Since OAM packets share fate with the normal traffic, identification at interface/parser is not possible: the statistics include both normal and OAM traffic.

* Sequence generator report:

```
    "name": {
        "name": "name",                         - name of the object
        "type": "seqgen",
        "use_init_flag": True/False,
        "use_reset_flag": True/False
        }
```
* Sequence recovery report:

```
    "name": {
        "discarded_packets": 0,
        *"history": "000000000000000",*         - only in debug mode
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
        "use_init_flag": True/False,
        "use_reset_flag": True/False
        }
```
   *History is only sent in debug mode.*

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
    The *delay_exceeded_packets* are the packets which arrived so late, that their delay is already exceeded at the moment of reception. This indicates that either the delay in the network increased significantly, or the *delay_ms* parameter in the configuration is too low.

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
Increasing number of "no match packets" indicates that traffic other than TSN/DetNet reaches the R2DTWO.
Usually we use TC filters or OVS to decouple the background traffic. However, in some cases traffic like ARP can still reach the R2DTWO. Excessive increase of "no match packets" indicates configuration error or misconfiguration of streams.

* TC report:

```
    "tc_interface": {
        json from TC output, specific to the TC qdisc used. The "tc -s -j qdisc show dev `interface` handle 0" command output is added here.
    }
```
### Notification message JSONs

The notification system uses a message fragmentation, which ensures that each fragment fits into an UDP packet.
To identify the fragments, two fields are used: the *notif_seq* and the *notif_fragment*. The *notif_seq* identifies the individual messages, which may be longer than 1200 byte. The *notif_fragment* tells how many fragments are in total, and also which fragment is the current.

```
    "notif_msg": { the actual json content of the message, fragmented in 1200 byte chunks }
    "notif_hostname": "hostname",
    "notif_seq": 0,    
    "notif_fragment": "1/2",
    "notif_tstamp": 1743628439.8999681
```
The *notif_message* holds the JSON message chuncks, and by concatenating the fragments we restore the original JSON message.
Note that even if no fragmentation is needed, the message still contains a *"notif_fragment": "1/1"* field.

In the following, we present the full message formats, without the *notif_fragment* field. This field is not needed after reassembly, so the *notification_receiver* class removes it.

##### Pull notifications

* Pull notification

```
"notif_msg": {
    source_1: object report 1
    source_2: object report 2

    source_n: object report n
}
"notif_hostname": "hostname",
"notif_seq": 0,
"notif_tstamp": 1743628439.8999681
```
Where source_1...n are the registered pull notification sources, that can be the ones described in the previous section.

##### Push notifications

* Configration file commit notification:

```
    "notif_msg": {
        "push_level": "INFO",
        "transaction": {"committed": "notification/notification-detnet.ini"}
    }
    "notif_hostname": "hostname",
    "notif_seq": 0,
    "notif_tstamp": 1743628439.8999681
```

Sent whenever R2DTWO commits a valid configuration.

* Startup notification:

```
    "notif_hostname": "hostname",
    "notif_seq": 1,
    "notif_tstamp": 1743628439.9162316,
    "notif_msg": {
        "push_level": "INFO",
        "r2dtwo": {"status": "startup completed"}
    }
```
Sent when R2DTWO is ready to process the incoming traffic.

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

## Limitations

The notification framework currently is limited to DetNet operation. There is no proper TSN support yet.
This means that:

* Notification messages should be sent on UDP-out interfaces
* MEP/MIP statistics are DetNet only (AutoMIP does not work with TSN)
* Also, trigger messages are DetNet OAM specific.

However, TSN over DetNet is fully supported.
