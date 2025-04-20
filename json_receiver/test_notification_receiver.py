import unittest
import json
from notification_receiver import NotificationReceiver

class TestNotificationReceiver(unittest.TestCase):
    def setUp(self):
        self.receiver = NotificationReceiver()

    def test_single_part_notification(self):
        data = json.dumps({
            "notif_seq": 1,
            "notif_hostname": "host1",
            "notif_fragment": "1/1",
            "notif_tstamp": "2024-01-01T00:00:00Z",
            "notif_msg": json.dumps({"message": "Hello"})
        })
        result = self.receiver.process_notification("127.0.0.1", 12345, data)
        self.assertIsNotNone(result)
        self.assertEqual(result["notif_msg"], {"message": "Hello"})
        result = self.receiver.process_notification("127.0.0.1", 12345, data)   # duplicate!
        self.assertIsNone(result)

    def test_duplicate_notification(self):
        data = json.dumps({
            "notif_seq": 2,
            "notif_hostname": "host2",
            "notif_fragment": "1/1",
            "notif_tstamp": "2024-01-01T00:00:01Z",
            "notif_msg": json.dumps({"message": "World"})
        })
        first_result = self.receiver.process_notification("127.0.0.1", 12345, data)
        second_result = self.receiver.process_notification("127.0.0.1", 12345, data)
        self.assertIsNotNone(first_result)
        self.assertIsNone(second_result)

    def test_multipart_complete(self):
        base_data = {
            "notif_seq": 3,
            "notif_hostname": "host3",
            "notif_tstamp": "2024-01-01T00:00:02Z",
        }

        # Simulate two fragments
        frag1 = base_data.copy()
        frag1["notif_fragment"] = "1/2"
        frag1["notif_msg"] = "{\"message\": "  # part 1 of JSON
        data1 = json.dumps(frag1)

        frag2 = base_data.copy()
        frag2["notif_fragment"] = "2/2"
        frag2["notif_msg"] = "\"Multipart complete\"}"
        data2 = json.dumps(frag2)

        result1 = self.receiver.process_notification("127.0.0.1", 12345, data1)
        self.assertIsNone(result1)

        result2 = self.receiver.process_notification("127.0.0.1", 12345, data2)
        self.assertIsNotNone(result2)
        self.assertEqual(result2["notif_msg"], {"message": "Multipart complete"})

if __name__ == '__main__':
    unittest.main()
