# Scenario: R2DTWO Notification Framework

__Important: this scenario assumes background knowledge of some other scenarios.
Please take a look into IP46oDetNet and OAM scenarios if you have not already.__

This scenario adds notifications to a IP46oDetNet scenario with OAM capabilities.

We will use the following topology, which consists:

* a talker node called **plague** which will generate IPv4 traffic
* a listener node called **dread** which receives the traffic coming from the **talker**
* two R2DTWO instances, running on the **pandora** and **minion** nodes.
* a node called __terror__ which interconnects the __pandora__ and __minion__ nodes, it forwards traffic between the neighbour nodes.

```
      ╔═══════╗                             ╔═══════╗    
      ║plague ║                             ║ dread ║    
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
│                │       │           │       │         │ 
│                │       │           │       │         │ 
│          ens2f1├───────│ens2f1     │       └╔═══════╗┘ 
└───╔═══════╗────┘       └─╔═══════╗─┘        ║minion ║  
    ║pandora║              ║terror ║          ╚═══════╝  
    ╚═══════╝              ╚═══════╝           R2DTWO    
     R2DTWO
```


Also, FRER/PREOF was intended to protect against path failures.
Therefore administration of R2DTWO over the production network would be a bad idea since it is expected to fail from time to time.

The __pandora__ node replicates the traffic arriving from the talker and the __minion__ node does the elimination.
(To have bi-directional traffic or the ping test, the backward direction is also configured: replication at the node __minion__ and elimination at the node __pandora__ for the traffic from the __listener__ towards the __talker__.)

## R2DTWO OAM configuration

The basic concepts and generic operation of the notification framework can be found in the doc folder of the repository.

In this guide, we discuss only the notification-related parts of the configuration that is specific for this scenario.

The notification aggregation point where the notification messages are sent is on the node __minion__ (eno1 interface, IP address: 10.10.10.2). To collect the the notification messages the simple json receiver will be run and collect the notification messages on this node.  
R2DTWO on node __pandora__ is configured to send its notification messages on two interfaces (ens2f0 and ans2f1) to have redundancy for the notification. 
R2DTWO on node __minion__ sends its notification messages without redundancy to the aggregation point (simple json receiver).

The corresponding configuration of `pandora.ini` related to notification is the following:

```
[interfaces]
ifNotify_ens2f0 = udp-out iface=ens2f0 dstip=10.10.10.2 dstport=6000
ifNotify_ens2f1 = udp-out iface=ens2f1 dstip=10.10.10.2 dstport=6000

[streams]
notification_session = send ifNotify_ens2f0, send ifNotify_ens2f1

```

The corresponding configuration of `minion.ini` related to notification is the following:

```
[interfaces]
ifNotify = udp-out iface=eno1 dstip=10.10.10.2 dstport=6000

[streams]
notification_session = send ifNotify

```


## Notification framework in action

This scenario runs in mininet. The `testnet.py` script initiates the network and the nodes and also starts the two R2DTWO instances, the notification receiver script and two telnet OAM session to each R2DTWO, altoghether 5 xterm is opened. 

The OAM telnet xterms' title shows the node name, and the following lines. In the following we reference these xterms as **oam-pandora** and **oam-minion**.
```
Trying 127.0.0.1...
Connected to localhost.
Escape character is '^]'.
OAM 'conn XY' ready
```

Anther two xterms show the logging output of the two R2DTWO instances, and the title of the xterm denotes the node name. 

The fifth xterm (with title __minion__) show sthe received notification messages as formatted JSON output. In the following we reference this xterm as **notif-recv**.

As the dafault setting is to disable pull notifications, R2DTWO startup related push notifications can be observed in **notif-recv** xterm (You may need to scroll back to the beginning in the xterm):

```
JSON receiver server started on 10.10.10.2 : 6000

Received 118 bytes from pandora , 192.168.1.2 : 56452 with sequence number 0
========== JSON data begin ==========
{
  "notif_hostname": "pandora",
  "notif_seq": 0,
  "notif_tstamp": 1743778867.2204173,
  "transaction": {
    "committed": "pandora.ini"
  }
}
........... JSON data end ...........
Message with sequence number  0 from  pandora  already received, not showing the replica

Received 116 bytes from pandora , 192.168.1.2 : 56452 with sequence number 1
========== JSON data begin ==========
{
  "notif_hostname": "pandora",
  "notif_seq": 1,
  "notif_tstamp": 1743778867.2249691,
  "r2dtwo": {
    "status": "startup completed"
  }
}
........... JSON data end ...........
Message with sequence number  1 from  pandora  already received, not showing the replica

Received 142 bytes from pandora , 192.168.1.2 : 56452 with sequence number 2
========== JSON data begin ==========
{
  "notif_hostname": "pandora",
  "notif_seq": 2,
  "notif_tstamp": 1743778868.1634479,
  "telnet": {
    "ip": "::ffff:127.0.0.1",
    "login": "conn 13",
    "port": 36532
  }
}
........... JSON data end ...........

```

Since node __pandora__ sends redundant notification messages on two interfaces the receiver detects the duplicates and does not display the replica by matching an already seen sequence number from a given R2DTWO. This is indicated by:

```
Message with sequence number  X from  <host>  already received, not showing the replica
```

### Pull notifications
To enable pull notifications enter the follwing command in **oam-pandora**:
```
notif_pull enable
```
The output in the next line shows:
```
Notification pull is now enabled
```

In **notif-recv** xterm the pull notification messages are shown from node __pandora__, and the replicated notification messages are not shown.
There is a lot of data and counters received with 2 second interval about the objects, interfaces, etc.

There are Maintanence Points defined in the configuration files for R2DTWO instances. For node __pandora__, direction talker -> listener:

- before the replication: c_pandora_tx 
- after the replication: path1_pandora_tx and path2_pandora_tx

#### User traffic statistics

For user traffic statistics look for octet (key "octets_passed") and packet (key "packets_passed") counters in the notification messages. Before sending any user traffic all these counters show 0 values:
```
  ...
  "c_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_pandora_tx",
    "oam_octets_passed": 201970,
    "oam_packets_passed": 575,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...
  "path1_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...
    "path2_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 0,
    "packets_passed": 0,
    "type": "mep_state"
  },
  ...  
``` 

Start an xterm for the talker from the mininet CLI:
```
mininet> xterm plague
```

Use the follwing command to send user traffic from the talker from the xterm started above:
```
ping -c 5 10.10.11.2
```

Hereafter look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters. Now the packet counters show the value of 5 since 5 ping request was sent, and the octets show 460:
```
  ...
  "c_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_pandora_tx",
    "oam_octets_passed": 248097,
    "oam_packets_passed": 706,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
    "path1_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
    "path2_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  '''
```

#### Failing one path

To investigate the case of a network failure the path going over the ens2f0 interfaces can be interrupted with the following command from the mininet CLI:
```
mininet> terror ip link set dev ens2f0 down
```
Then we send again 5 ping request from the talker (xterm plague):
```
ping -c 5 10.10.11.2
```
Observations:
The ping request (and responses) are still transferred over the network via the remaining redundant path.

Hereafter look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters. 
Now the counters show 10 packets and 920 octets, since the failure is after this node, it tries to send all packets on both paths.

```
  ...
  "c_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_pandora_tx",
    "oam_octets_passed": 718948,
    "oam_packets_passed": 2033,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path1_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path2_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  '''
```

To investigate the elimination side, enable the pull notifications for node __minion__ in xterm **oam-minion**:
```
notif_pull enable
```

The Maintanence Points defined for node __minion__, direction talker -> listener:

- before the elimination: path1_minion_rx and path2_minion_rx
- after the elimination: c_minion_rx 

In the notification messages from node __minion__ look for the octet (key "octets_passed") and packet (key "packets_passed") counters. 
The counters show 10 packets and 920 octets, but only for c_minion_rx and path2_minion_rx, and only 5 packets and 460 octets for path1_minion_rx since this is the receiving side of the failing path.

```
  ...
  "c_minion_rx": {
    "mask_signal_state": "unmasked",
    "name": "c_minion_rx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path1_minion_rx": {
    "mask_signal_state": "unmasked",
    "name": "path1_minion_rx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 460,
    "packets_passed": 5,
    "type": "mep_state"
  },
  ...
  "path2_minion_rx": {
    "mask_signal_state": "unmasked",
    "name": "path2_minion_rx",
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
mininet> terror ip link set dev ens2f0 up
```

After restoring the path, duplicated notification messages are received agin from node __pandora__, therefore in the __notif-recv__ xterm these are indicated by such messages:
```
Message with sequence number  XYZ from  pandora  already received, not showing the replica
```

#### Masking a path

To mask path1, use the following comannd in __oam-pandora__:
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

Then we send again 5 ping request from the talker (xterm plague):
```
ping -c 5 10.10.11.2
```

Look again in the notification messages for the octet (key "octets_passed") and packet (key "packets_passed") counters at node __pandora__.
 Now the packet counters for c_pandora_tx and path2_pandora_tx show the value of 15 and the octets show 1380, however, for path1_pandora_tx it remains 10 and 920 since this path is masked, therefore packet were not sent on this path.
```
  ...
  "c_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "c_pandora_tx",
    "oam_octets_passed": 1148024,
    "oam_packets_passed": 3238,
    "octets_passed": 1380,
    "packets_passed": 15,
    "type": "mep_state"
  },
  ...
  "path1_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path1_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 920,
    "packets_passed": 10,
    "type": "mep_state"
  },
  ...
  "path2_pandora_tx": {
    "mask_signal_state": "unmasked",
    "name": "path2_pandora_tx",
    "oam_octets_passed": 0,
    "oam_packets_passed": 0,
    "octets_passed": 1380,
    "packets_passed": 15,
    "type": "mep_state"
  },
  '''
```

Restore the original state by unmasking path1 with the following comannd in __oam-pandora__:
```
unmask tx1
```

#### Disable pull notifications



#### Notification trigger
TODO