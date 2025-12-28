class Monitor:

    def start(self):
        self.events = []

    def record(self, event):
        self.events.append(event)

    def collect(self):
        return {
            "events": self.events,
            "status": "completed"
        }
