from abc import ABC, abstractmethod

class FaultInjector(ABC):

    @abstractmethod
    def setup(self):
        pass

    @abstractmethod
    def inject(self, fault):
        pass

    @abstractmethod
    def teardown(self):
        pass