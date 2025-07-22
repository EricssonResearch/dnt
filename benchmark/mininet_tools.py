from mininet.net import Mininet
from mininet.cli import CLI

def create_net(topo_type):
    match topo_type:
        case "1hop":
            """
            network with talker and listener and 1 intermediate node

                talker ---- n-----listener

            192.168.1.1                    192.168.2.1

            """
            net = Mininet(autoStaticArp=True)

            # nodes: n, talker, listener
            t = net.addHost('t', ip='192.168.1.1/24')
            l =  net.addHost('l', ip='192.168.2.1/24')
            n = net.addHost('n', ip=None)

            # links
            net.addLink(t, n, intfName1='eth0', intfName2='eth1')
            net.addLink(l, n, intfName1='eth0', intfName2='eth2')

            net.build()

            return net

        case "2hop":

            """
            network with talker and listener and 2 intermediate nodes with dual connection between them
                            --------
                            /        \
                talker ---- n1--------n2----listener
            192.168.1.1                    192.168.2.1

            """
            net = Mininet(autoStaticArp=True)

            # nodes: n1, n2, talker, listener
            t = net.addHost('t', ip='192.168.1.1/24')
            l =  net.addHost('l', ip='192.168.2.1/24')
            n1 = net.addHost('n1', ip=None)
            n2 = net.addHost('n2', ip=None)

            # links
            net.addLink(t, n1, intfName1='eth0', intfName2='eth0')
            net.addLink(n1, n2, intfName1='eth1', intfName2='eth1')
            net.addLink(n1, n2, intfName1='eth2', intfName2='eth2')
            net.addLink(l, n2, intfName1='eth0', intfName2='eth0')

            net.build()
            return net

        case _ :
            print("Unknown network type, use \"1hop\" or \"2hop\"")

def config_net_fw(net):
    # configure network
    t, l = [net.get(n) for n in ["t", "l"]]

    t.cmd("ethtool -K eth0 tx off rx off")
    l.cmd("ethtool -K eth0 tx off rx off")

    t.cmd("ip r add default dev eth0")
    l.cmd("ip r add default dev eth0")



def config_net_routing(net, topo_type):
    match topo_type:
        case "1hop":
            """
            network with talker and listener and 1 intermediate node

                talker ---- n-----listener

            192.168.1.1                    192.168.2.1

            """
            # configure network
            t, l, n = [net.get(n) for n in ["t", "l", "n"]]
            n.cmd(f"ip a a 10.0.0.1/32 dev lo")
            n.cmd("sysctl -w net.ipv4.ip_forward=1")

            t.cmd("ethtool -K eth0 tx off rx off")
            l.cmd("ethtool -K eth0 tx off rx off")
            n.cmd("ip a a 192.168.1.2/24 dev eth1")
            n.cmd("ip a a 192.168.2.2/24 dev eth2")

            t.cmd("ip r add default via 192.168.1.2")
            l.cmd("ip r add default via 192.168.2.2")

        case "2hop":
            """
            network with talker and listener and 2 intermediate nodes with dual connection between them
                            --------
                            /        \
                talker ---- n1--------n2----listener
            192.168.1.1                    192.168.2.1

            """
            # configure network
            t, l, n1, n2 = [net.get(n) for n in ["t", "l", "n1", "n2"]]

            ip_lo = 1
            for n in [n1, n2 ]:
                n.cmd(f"ip a a 10.0.0.{ip_lo}/32 dev lo")
                n.cmd("sysctl -w net.ipv4.ip_forward=1")
                ip_lo += 1
            t.cmd("ethtool -K eth0 tx off rx off")
            l.cmd("ethtool -K eth0 tx off rx off")
            n1.cmd("ip a a 192.168.1.2/24 dev eth0")
            n1.cmd("ip a a 10.0.11.1/24 dev eth1")
            n1.cmd("ip a a 10.0.22.1/24 dev eth2")
            n2.cmd("ip a a 192.168.2.2/24 dev eth0")
            n2.cmd("ip a a 10.0.11.2/24 dev eth1")
            n2.cmd("ip a a 10.0.22.2/24 dev eth2")


            # routing
            n1.cmd("ip r add 192.168.2.0/24 via 10.0.11.2 metric 5")
            n1.cmd("ip r add 192.168.2.0/24 via 10.0.22.2 metric 10")
            n2.cmd("ip r add 192.168.1.0/24 via 10.0.11.1 metric 5")
            n2.cmd("ip r add 192.168.1.0/24 via 10.0.22.1 metric 10")

            t.cmd("ip r add default via 192.168.1.2")
            l.cmd("ip r add default via 192.168.2.2")

        case _ :
            print("Unknown network type, use \"1hop\" or \"2hop\"")

def config_net_mtu(net, topo_type):
    match topo_type:
        case "1hop":
            """
            network with talker and listener and 1 intermediate node

                talker ---- n-----listener

            192.168.1.1                    192.168.2.1

            """
            # configure network
            t, l, n = [net.get(n) for n in ["t", "l", "n"]]

            # MTU setting
            n.cmd("ip l set dev eth1 mtu 2000 up")
            n.cmd("ip l set dev eth2 mtu 2000 up")

        case "2hop":

            """
            network with talker and listener and 2 intermediate nodes with dual connection between them
                            --------
                            /        \
                talker ---- n1--------n2----listener
            192.168.1.1                    192.168.2.1

            """
            # configure network
            t, l, n1, n2 = [net.get(n) for n in ["t", "l", "n1", "n2"]]

            # MTU setting
            n1.cmd("ip l set dev eth1 mtu 2000 up")
            n1.cmd("ip l set dev eth2 mtu 2000 up")
            n2.cmd("ip l set dev eth1 mtu 2000 up")
            n2.cmd("ip l set dev eth2 mtu 2000 up")

        case _ :
            print("Unknown network type, use \"1hop\" or \"2hop\"")



