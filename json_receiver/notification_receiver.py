import json
from collections import deque


class NotificationReceiver:
    def __init__(self, seq_history_size=20):
        """
        Initialize NotificationReceiver. Optional parameter is
        @seq_history_size, the size of the history window, default value is 20.
        """
        self.SEQ_HISTORY_SIZE = seq_history_size
        self.last_seqnums = deque(maxlen=self.SEQ_HISTORY_SIZE)
        self.mpart_collector = {}

    def process_notification(self, host, port, data):
        """
        Performs the reassembly of message fragments and also elimination
        of duplicate messages. Returns only full json messages.
        @host, @port - sender host and port, needed for elimination
        @data - the received json fragment
        @return None if message is partial, or the full json message.
        """
        try:
            jsonReceived = self._parse_json(data)
            self._validate_input(jsonReceived)

            seq_num = jsonReceived.get("notif_seq")
            hostname = jsonReceived.get("notif_hostname")
            frag_id = jsonReceived.get("notif_fragment")
            timestamp = jsonReceived.get("notif_tstamp")

            if self._is_duplicate(hostname, seq_num, str(frag_id)):
                return None

            self.last_seqnums.append((hostname, seq_num, str(frag_id)))

            if frag_id is None or frag_id == "1/1":
                return self._handle_single_part(jsonReceived)

            return self._handle_multipart(jsonReceived, hostname, seq_num, frag_id, timestamp, port)

        except json.decoder.JSONDecodeError:
            print("JSON decoder error: received message is not JSON or buffer is too small")
        except ValueError as ve:
            print(f"Invalid input: {ve}")

        return None

    def _parse_json(self, data):
        return json.loads(data)

    def _validate_input(self, message):
        required_fields = ["notif_seq", "notif_hostname", "notif_msg", "notif_tstamp"]
        for field in required_fields:
            if field not in message:
                raise ValueError(f"Missing required field: {field}")

    def _is_duplicate(self, hostname, seq_num, frag_id):
        return (hostname, seq_num, frag_id) in self.last_seqnums

    def _handle_single_part(self, message):
        message["notif_msg"] = json.loads(message["notif_msg"])
        return message

    def _handle_multipart(self, jsonReceived, hostname, seq_num, frag_id, timestamp, port):
        if hostname not in self.mpart_collector:
            self.mpart_collector[hostname] = {
                "message": {},
                "last_seqnum": -1,
                "frag_count": 0,
            }

        idx = int(frag_id.split('/')[0])
        items = int(frag_id.split('/')[1])
        collector = self.mpart_collector[hostname]

        if collector["last_seqnum"] != seq_num:
            if collector["frag_count"] > 0:
                print(f"\nWARNING: Missing part(s) of multipart message from {hostname} : {port} with sequence number {collector['last_seqnum']}")
            collector["last_seqnum"] = seq_num
            collector["message"].clear()
            collector["frag_count"] = 0

        collector["message"][idx] = jsonReceived.get("notif_msg")
        collector["frag_count"] += 1

        if collector["frag_count"] == items:
            concat_string = ""
            for i in range(1, items + 1):
                part = collector["message"].get(i)
                if part is None:
                    print(f"\nWARNING: Missing part(s) of multipart message reassembly from {hostname} : {port} with sequence number {seq_num}")
                    return None
                concat_string += part

            fullmsg_with_header = {
                "notif_seq": seq_num,
                "notif_hostname": hostname,
                "notif_tstamp": timestamp,
                "notif_msg": json.loads(concat_string)
            }

            collector["last_seqnum"] = -1
            collector["message"].clear()
            collector["frag_count"] = 0

            return fullmsg_with_header

        return None
