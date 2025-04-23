# Scenario: R2DTWO Notification Framework

__Important: this scenario assumes background knowledge of some other scenarios.
Please take a look into IP46oDetNet and OAM scenarios if you have not already.__

This scenario adds notifications to a IP46oDetNet scenario with OAM capabilities.

We will use the following topology, which consists:

* a talker node called **host1** which will generate IPv4 traffic
* a listener node called **host2** which receives the traffic coming from the **talker**
* two R2DTWO instances, running on the **edge1** and **edge2** nodes.
* a node called __core__ which interconnects the __edge1__ and __edge2__ nodes and forwards the traffic between them.

```
      ╔═══════╗                             ╔═══════╗    
      ║ host1 ║                             ║ host2 ║    
  ┌───╚═══════╝───┐                      ┌──╚═══════╝───┐
  │   (talker)    │                      │  (listener)  │
  │               │                      │              │
  │ue0:192.168.2.2│                      │ue0:10.10.11.2│
  └────┬──────────┘                      └─────────┬────┘
       │                                           │     
       │                                           │     
┌──────┴─────────┐       ┌───────────┐             │     
│    eno1        │       │           │             │     
│                │       │           │       ┌─────┴───┐
│          ens2f0├───────┤ens2f0     │       │   eno2  │
│                │       │           │       │         │
│                │       │       eno1├───────┤eno1     │
│          ens2f1├───────│ens2f1     │       │         │
│                │       │           │       │  (pef)  │
│     (prf)      │       │           │       └╔═══════╗┘
└───╔═══════╗────┘       └─╔═══════╗─┘        ║ edge2 ║
    ║ edge1 ║              ║ core  ║          ╚═══════╝
    ╚═══════╝              ╚═══════╝           R2DTWO
     R2DTWO
```

The __edge1__ node replicates the traffic arriving from the talker and the __edge2__ node does the elimination.
(To have bi-directional traffic /e.g. ping test/, the backward direction is also configured: for the traffic from the __listener__ towards the __talker__ replication is at the node __edge2__ and elimination is at the node __edge1__ .)

## R2DTWO OAM configuration

The basic concepts and generic operation of the notification framework can be found in the doc folder of the repository.

In this guide, we discuss only the notification-related parts of the configuration that is specific for this scenario.

The notification aggregation point where the notification messages are sent to and collected is on the node __edge2__ (eno1 interface, IP address: 10.10.10.2). To collect the the notification messages a simple json receiver will be run on this node.  
R2DTWO on node __edge1__ is configured to send its notification messages on two interfaces (ens2f0 and ans2f1) to have redundancy for the notification.
There is also a continuous oam ping initiated from c_edge1_tx to any destination, this will generate oam traffic that can be monitored with the oam packet and octet counters.
R2DTWO on node __edge2__ sends its notification messages without redundancy to the same aggregation point (the simple json receiver collects messages from both nodes).

The corresponding configuration of `edge1.ini` related to notification is the following:

```
[interfaces]
ifNotify_ens2f0 = udp-out iface=ens2f0 dstip=10.10.10.2 dstport=6000
ifNotify_ens2f1 = udp-out iface=ens2f1 dstip=10.10.10.2 dstport=6000

[streams]
notification_session = send ifNotify_ens2f0, send ifNotify_ens2f1

```

The corresponding configuration of `edge2.ini` related to notification is the following:

```
[interfaces]
ifNotify = udp-out iface=eno1 dstip=10.10.10.2 dstport=6000

[streams]
notification_session = send ifNotify

```


## Notification framework in action

This scenario runs in mininet. The `testnet.py` script creates the network topology with the nodes. It also starts the two R2DTWO instances, the notification receiver script and two telnet OAM session to each R2DTWO in separate xterms, altoghether 5 xterm is opened.
The `testnet.py` script has an optional input parameter, *AutoMIP*. When *AutoMIP* is specified, a different configuration is used that uses AutoMIP to set up MIPs automatically for each replication/elimination object. With *AutoMIP* enabled, the MIP/MEP names are automatically generated, thus the OAM commands will be different. In this document for each OAM command, the AutoMIP command is also shown.

The OAM telnet xterms' title shows the node name, and the following lines. In the following we reference these xterms as **oam-edge1** and **oam-edge2**.
```
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
OAM 'conn XY' ready
```

Anther two xterms show the logging output of the two R2DTWO instances, and the title of the xterm denotes the node name.

The fifth xterm (with title __edge2__) show sthe received notification messages as formatted JSON output. In the following we reference this xterm as **notif-recv**.

As the dafault setting is to disable pull notifications, only push notifications are shown: loading the configuration file, startup completed, and telnet session onnected can be observed in **notif-recv** xterm (You may need to scroll back to the beginning of the messages in the xterm):

```
JSON receiver server started on 10.10.10.2 : 6000

Received 218 bytes from edge1 , 192.168.1.2 : 36951 with sequence number 0
========== JSON data begin ==========
{
  "notif_hostname": "edge1",
  "notif_msg": {
    "push_level": "INFO",
    "transaction": {
      "add_ifaces": 10,
      "add_objects": 3,
      "add_streams": 3,
      "committed": "edge1.ini"
    }
  },
  "notif_seq": 0,
  "notif_tstamp": 1744037778.721275
}
........... JSON data end ...........
Message with sequence number  0 from  edge1  with fragment ID  None  already received, not showing the replica

Received 162 bytes from edge1 , 192.168.1.2 : 36951 with sequence number 1
========== JSON data begin ==========
{
  "notif_hostname": "edge1",
  "notif_msg": {
    "push_level": "INFO",
    "r2dtwo": {
      "status": "startup completed"
    }
  },
  "notif_seq": 1,
  "notif_tstamp": 1744037778.7405703
}
........... JSON data end ...........
Message with sequence number  1 from  edge1  with fragment ID  None  already received, not showing the replica

Received 194 bytes from edge1 , 192.168.1.2 : 36951 with sequence number 2
========== JSON data begin ==========
{
  "notif_hostname": "edge1",
  "notif_msg": {
    "push_level": "INFO",
    "telnet": {
      "ip": "::ffff:127.0.0.1",
      "login": "conn 13",
      "port": 36354
    }
  },
  "notif_seq": 2,
  "notif_tstamp": 1744037779.6541355
}
........... JSON data end ...........
Message with sequence number  2 from  edge1  with fragment ID  None  already received, not showing the replica
```

Since node __edge1__ sends redundant notification messages on two interfaces, the receiver detects the duplicates and does not display the replica by matching an already seen sequence number from a given R2DTWO. This is indicated by:

```
Message with sequence number  X from  <host>  with fragment ID  None already received, not showing the replica
```

### Pull notifications
To enable pull notifications enter the follwing command in **oam-edge1**:
```
notif_pull enable
```
The output in the next line shows:
```
Notification pull is now enabled
```

In **notif-recv** xterm the pull notification messages are shown from node __edge1__, and the replicated notification messages are not shown.
There is a lot of data and counters received with 2 second interval about the objects, interfaces, etc.

There are Maintanence Points defined in the configuration files of the R2DTWO instances. For node __edge1__, in direction talker -> listener:

- before the replication: c_edge1_tx
- after the replication: path1_edge1_tx and path2_edge1_tx

AutoMIP version:
- before the replication: o_stream_uni_L3_pre-prfe1
- after the replication: o_tx1_L3_post-prfe1 and o_tx2_L3_post-prfe1


#### Traffic statistics

For user traffic statistics look for octet (key "octets_passed") and packet (key "packets_passed") counters in the notification messages. Before sending any user traffic all these counters show 0 values:
```
  ...
  "c_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_edge1_tx",
    "oam_octets_passed": 201970,
    "oam_packets_passed": 575,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...
  "path1_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...
    "path2_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...  
```

The oam traffic counters are also 0, except in c_edge1_tx, because there is a oam ping session configured in `edge1.ini` shown below. These counters will increase in the following samples as the oam ping is running continuously.
```
[oam]
compound_ping = ping c_edge1_tx any 3 -o -i 2
```

AutoMIP version:
```
[oam]
compound_ping = ping o_stream_uni_L3_pre-prfe1 o_mcompound_L3_post-pefe2 3 -o -i 2
```

Start an xterm for the talker node from the mininet CLI for sending user traffic from talker:
```
mininet> xterm host1
```

Use the follwing command to send user traffic from the talker from the xterm started above:
```
ping -c 5 10.10.11.2
```

Hereafter look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters. Now the packet counters show the value of 5 since 5 ping request was sent, and the octets show 460:
```
  ...
  "c_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_edge1_tx",
    "oam_octets_passed": 248097,
    "oam_packets_passed": 706,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
    "path1_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
    "path2_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  '''
```

#### Failing one path

To investigate the case of a network failure, the path going over the ens2f0 interfaces between nodes **host1** and **core** can be interrupted with the following command from the mininet CLI:
```
mininet> core ip link set dev ens2f0 down
```
Then we send again 5 ping request from the talker (xterm host1):
```
ping -c 5 10.10.11.2
```
Observations:
The ping requests (and responses) are still transferred over the network via the remaining redundant path.

Hereafter look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters.
Now the counters show 10 packets and 920 octets, since the failure is after this node, it tries to send all packets on both paths.

```
  ...
  "c_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_edge1_tx",
    "oam_octets_passed": 718948,
    "oam_packets_passed": 2033,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path1_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path2_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  '''
```

To investigate the elimination side, enable the pull notifications for node __edge2__ in xterm **oam-edge2**:
```
notif_pull enable
```

The Maintanence Points defined for node __edge2__, in direction talker -> listener:

- before the elimination: path1_edge2_rx and path2_edge2_rx
- after the elimination: c_edge2_rx

In the notification messages from node __edge2__ look for the octet (key "octets_passed") and packet (key "packets_passed") counters.
The counters show 10 packets and 920 octets, but only for c_edge2_rx and path2_edge2_rx, and only 5 packets and 460 octets for path1_edge2_rx since this is the receiving side of the failing path.

```
  ...
  "c_edge2_rx": {
    "mask_signal_state": "unmasked",
    "name": "c_edge2_rx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path1_edge2_rx": {
    "mask_signal_state": "unmasked",
    "name": "path1_edge2_rx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
  "path2_edge2_rx": {
    "mask_signal_state": "unmasked",
    "name": "path2_edge2_rx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  '''
```

#### Restoring the failed path
To restore the failed path, use the following command from the mininet CLI:
```
mininet> core ip link set dev ens2f0 up
```

After restoring the path, duplicated notification messages are received agin from node __edge1__, therefore in the __notif-recv__ xterm these are indicated by such messages:
```
Message with sequence number  XYZ from  edge1  already received, not showing the replica
```

#### Masking a path

To mask path1, use the following comannd in __oam-edge1__:
```
mask tx1
```

The masked state is showing up in the packet replication function (prf) state:
```
  "prf": {
    "name": "prf",
    "octets_passed": 1108592,
    "packets_passed": 3135,
    "pipelines": [
      {
        "action_count": 3,
        "mask_state": "masked",
        "name": "tx1"
      },
      {
        "action_count": 3,
        "mask_state": "unmasked",
        "name": "tx2"
      }
    ],
    "type": "replicate"
  },
```

Then we send again 5 ping request from the talker (xterm host1):
```
ping -c 5 10.10.11.2
```

Look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters at node __edge1__.
 Now the packet counters for c_edge1_tx and path2_edge1_tx show the value of 15 and the octets show 1380, however, for path1_edge1_tx it remains 10 and 920 since this path is masked, therefore packet were not sent on this path.
```
  ...
  "c_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_edge1_tx",
    "oam_octets_passed": 1148024,
    "oam_packets_passed": 3238,
    "octets_passed": 1380,
    "packets_passed": 15,
    "type": "mep_state"
  },
  ...
  "path1_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path2_edge1_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_edge1_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 1380,
    "packets_passed": 15,
    "type": "mep_state"
  },
  '''
```

Restore the original state by unmasking path1 with the following comannd in __oam-edge1__:
```
unmask tx1
```

#### Notification trigger

The notification trigger initiates push notifications at the trigger source and receiver nodes, therefore let's disable pull notifications for both node __edge1__ in xterm **oam-edge1** and node __edge2__ in xterm **oam-edge2**:
```
notif_pull disable
```

We initate a notification trigger from c_edge1_tx (before replication) towards c_edge2_rx (after elimination) in xterm **oam-edge1** with the following command:
```
notif_trigger c_edge1_tx c_edge2_rx 3 -n 1
```

AutoMIP version:
```
notif_trigger o_stream_uni_L3_pre-prfe1 o_mcompound_L3_post-pefe2 3 -n 1

```

The notif_msg section of the push message from node __edge1__ (triggered_source) contains all relevant information about maintenance point states and counters:

```
========== JSON data begin ==========
{
  "notif_hostname": "edge1",
  "notif_msg": {
    "push_level": "INFO",
    "triggered_source": {
      "level": 3,
      "mep": [
        {
          "mask_signal_state": "unmasked",
          "name": "c_edge1_tx",
          "oam_octets_passed": 364089,
          "oam_packets_passed": 1036,
          "octets_passed": 0,
          "packets_passed": 0,
          "type": "mep_state"
        },
        {
          "mask_signal_state": "unmasked",
          "name": "path1_edge1_tx",
          "oam_octets_passed": 0,
          "oam_packets_passed": 0,
          "octets_passed": 0,
          "packets_passed": 0,
          "type": "mep_state"
        },
        {
          "name": "prf",
          "octets_passed": 364089,
          "packets_passed": 1036,
          "pipelines": [
            {
              "action_count": 3,
              "mask_state": "unmasked",
              "name": "tx1"
            },
            {
              "action_count": 3,
              "mask_state": "unmasked",
              "name": "tx2"
            }
          ],
          "type": "replicate"
        }
      ],
      "node_id": 1,
      "seq": 0,
      "session": 2,
      "source": "c_edge1_tx",
      "stream": "stream_uni",
      "target": "c_edge2_rx"
    }
  },
  "notif_seq": 1407,
  "notif_tstamp": 1744039850.6794293
}
........... JSON data end ...........
```

The notif_msg section of the push message from node __edge2__ (triggered_receiver) contains all relevant information about maintenance point states and counters:

```
========== JSON data begin ==========
{
  "notif_hostname": "edge2",
  "notif_msg": {
    "push_level": "INFO",
    "triggered_receiver": {
      "level": 3,
      "mep": [
        {
          "mask_signal_state": "unmasked",
          "name": "c_edge2_rx",
          "oam_octets_passed": 0,
          "oam_packets_passed": 0,
          "octets_passed": 0,
          "packets_passed": 0,
          "type": "mep_state"
        },
        {
          "mask_signal_state": "unmasked",
          "name": "path2_edge2_rx",
          "oam_octets_passed": 0,
          "oam_packets_passed": 0,
          "octets_passed": 0,
          "packets_passed": 0,
          "type": "mep_state"
        },
        {
          "discarded_packets": 0,
          "history_length": 512,
          "latent_error_paths": 2,
          "latent_error_resets": 0,
          "latent_errors": 0,
          "name": "pef",
          "passed_packets": 0,
          "recovery_algorithm": "vector",
          "recovery_seq_num": 65535,
          "reset_msec": 2000,
          "seq_recovery_resets": 1,
          "type": "seqrec",
          "use_init_flag": false,
          "use_reset_flag": false
        }
      ],
      "node_id": 1,
      "seq": 0,
      "session": 2,
      "source": "c_edge1_tx",
      "stream": "stream_uni",
      "target": "c_edge2_rx"
    }
  },
  "notif_seq": 4,
  "notif_tstamp": 1744039850.679749
}
........... JSON data end ...........      
```
