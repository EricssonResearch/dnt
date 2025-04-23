# R2DTWO notification receiver

This directory contains a python library and examples for receiving and handling R2DTWO notifications.
The notifications are sent via UDP in json format, fragmented into 1200 byte chunks.

To identify the fragments, two fields are used: the *notif_seq* and the *notif_fragment*. The *notif_seq* identifies the individual messages, which may be longer than 1200 byte. The *notif_fragment* tells how many fragments are in total, and also which fragment is the current.

```json
{
    "notif_msg": "the actual json content of the message, fragmented in 1200 byte chunks",
    "notif_hostname": "hostname",
    "notif_seq": 0,
    "notif_fragment": "1/2",
    "notif_tstamp": 1743628439.8999681
}
```

- *json_udp_receiver* is a receiver that displays the messages _as is_, without reassembling the fragments.
However, elimination of multiple messages is performed.
- *notification_receiver* is a python library that performs both the elimination of duplicates and reassembly of the messages.
- *multipart_json_udp_receiver* is a receiver based on the *notification_receiver* library that receives the fragments and displays the reassembled messages.

## Usage

The *notification_receiver.py* provides a *NotificationReceiver* class, which implements all the required functionality. To use the class, first we need to instantiate a receiver:

```python
receiver = NotificationReceiver()
```

Then, for each received message the *process_notification* method is called. This method either returns the full message, or returns "None" when the received message is still incomplete.

```python
json_received = receiver.process_notification(host_ip, port, data)
if json_received is not None:
    print(json.dumps(jsonReceived, indent=2))   # display the message
```

The library has the following limitations:

* No reordering of messages is allowed. While parts of a message are ordered, fragments of different messages are not handled. When message with a new *notif_seq* number arrives, the previous incomplete message is discarded.
* Fragment loss results in the loss of the whole message. (Note that fragment loss means that the fragment is lost from all sending sources)
* *notif_hostname* is used to identify the message sources. When running in Mininet environment it is important to override the hostname to R2DTWO by specifying the "-h hostname" parameter, because all Mininet nodes share the host's hostname.

