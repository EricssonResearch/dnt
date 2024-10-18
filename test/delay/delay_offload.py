#!/usr/bin/python3

from utils import *
import time
import argparse

NSEC_PER_MSEC = 1000000
DELAY = 400
DELTAS = [300000, 300000000]
PING_TYPES = ['single', 'burst']
PROTO = ['eth', 'udp']

namespaces = []
stdouts = { }

# Veth test variables
veth_pairs = [('swp2', 'nxp1', 'eth0', 'talker'),
              ('swp0', 'nxp1', 'swp0', 'nxp2'),
              ('swp2', 'nxp2', 'eth0', 'listener')]
veth_ifaces = [('talker', '10.0.0.1/24', 'eth0'),
              ('listener', '10.0.0.2/24', 'eth0'),
              ('nxp1', '192.168.55.1/24', 'swp0'),
              ('nxp2', '192.168.55.2/24', 'swp0'),
              ('nxp1', '10.0.0.111/24', 'swp2'),
              ('nxp2', '10.0.0.222/24', 'swp2')]
config = ["good config", "wrong priority", "missing offload"]

# Physical test variables
nsx_physical_ifaces = ['enp3s0', 'enp4s0', 'enp6s0', 'enp7s0']
nsx_veth_ifaces = ['aeth0', 'beth0']
tx_lx_ifaces = [('talker', 'teth0', '10.0.0.1/24', '00:00:00:01:01:01', '00:00:00:02:02:02', '10.0.0.2'),
                ('listener', 'leth0', '10.0.0.2/24', '00:00:00:02:02:02', '00:00:00:01:01:01', '10.0.0.1')]


def ping_check_time(ping_output, bounds) -> bool:
    times_str = [s for s in ping_output.splitlines() if "icmp_seq" in s]
    times_f = [float(s.split(" ")[6].split("=")[1]) for s in times_str]
    res_bool = [(f >= bounds[0] and f <= bounds[1])  for f in times_f]
    return all(res_bool)


def log_test_details(protocol, offload, ping_type, delta):
    if ping_type == "single":
        ping_str = "single packets"
    else:
        ping_str = "back-to-back packets"
    
    if offload:
        print(f" • Delay offload {protocol} test with {ping_str} and {delta} ms delta", end=" ", flush=True)
    else:
        print(f" • Delay test with {ping_str}", end=" ", flush=True)


def start_R2DTWO(protocol, conf):
    if protocol == "eth":
        if conf == "wrong priority":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/eth_wrong_prio.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/eth_wrong_prio.ini")
        elif conf == "missing offload":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/eth_missing_offload.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/eth_missing_offload.ini")
        elif conf == "good config":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/eth.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/eth.ini")
    elif protocol == "udp":
        if conf == "wrong priority":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/udp_nxp1_wrong_prio.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/udp_nxp2_wrong_prio.ini")
        elif conf == "missing offload":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/udp_nxp1_missing_offload.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/udp_nxp2_missing_offload.ini")
        elif conf == "good config":
            exec_bg("ip netns exec nxp1 ../r2dtwo delay/udp_nxp1.ini")
            exec_bg("ip netns exec nxp2 ../r2dtwo delay/udp_nxp2.ini")


def stop_R2DTWO():
    exec_fg("killall r2dtwo")


def run_offload_veth_tests(protocol, conf, ping_type, delta) -> int:
    log_test_details(protocol, True, ping_type, delta)
    exec_fg(f"ip netns exec talker ping 10.0.0.2 -c 1")  # first ping for ARP
    num_pings = 5
    if ping_type == 'single':
        pingcmd = exec_fg(f"ip netns exec talker ping 10.0.0.2 -c {num_pings}")
    elif ping_type == 'burst':
        pingcmd = exec_fg(f"ip netns exec talker ping 10.0.0.2 -c {num_pings} -i 0.05")
    
    if conf != "missing offload" and pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
        print('\n', pingcmd.stdout)
        return 0
    elif conf == "missing offload" and pingcmd.stdout and f", 0 received," not in pingcmd.stdout:
        print('\n', pingcmd.stdout)
        return 0
    
    bound = (DELAY - delta - 2, DELAY - delta + 2)
    bound_wrong_prio = (0, 2)
    if conf == "good config" and ping_check_time(pingcmd.stdout, bound) == False:
        print('\n', pingcmd.stdout)
        print("It should be between: ", bound)
        return 0
    elif conf == "wrong priority" and ping_check_time(pingcmd.stdout, bound_wrong_prio) == False:
        print('\n', pingcmd.stdout)
        print("It should be between: ", bound_wrong_prio)
        return 0
    
    return 1


def run_offload_physical_tests(ping_type, delta) -> int:
    log_test_details('eth', True, ping_type, delta)
    exec_bg("ip netns exec nsx ../r2dtwo delay/physical_send.ini")
    exec_bg("ip netns exec nsx ../r2dtwo delay/physical_recv.ini")

    time.sleep(0.5)
    exec_fg(f"ip netns exec talker ping 10.0.0.2 -c 3")  # first ping for ARP
    num_pings = 5
    if ping_type == 'single':
        pingcmd = exec_fg(f"ip netns exec talker ping 10.0.0.2 -c {num_pings}")
    elif ping_type == 'burst':
        pingcmd = exec_fg(f"ip netns exec talker ping 10.0.0.2 -c {num_pings} -i 0.001")
    
    if pingcmd.stdout and f", {num_pings} received," not in pingcmd.stdout:
        print('\n', pingcmd.stdout)
        exec_fg("killall r2dtwo")
        return 0
    
    bound = (DELAY - 2, DELAY + 2)
    if ping_check_time(pingcmd.stdout, bound) == False:
        print('\n', pingcmd.stdout)
        print("It should be between: ", bound)
        exec_fg(f"killall r2dtwo")
        return 0
    
    exec_fg(f"killall r2dtwo")
    return 1


def config_veth() -> bool:
    ret = exec_fg(f"ip netns")
    if ret.stdout != '':
        for ns in namespaces:
            if ns in ret.stdout:
                print(f"Error: {ns} namespace is exists. Maybe run `ip -all netns delete`?")
                return False

    ret = 0
    for ns in namespaces:
        ret += exec_fg(f"ip netns add {ns}").returncode

    for pairs in veth_pairs:
        ret += exec_fg(f"ip link add {pairs[0]} numtxqueues 4 netns {pairs[1]} type veth peer {pairs[2]} numtxqueues 4 netns {pairs[3]}").returncode

    for ns in namespaces:
        ret += exec_fg(f"ip netns exec {ns} ip link set dev lo up").returncode

    for iface in veth_ifaces:
        ret += exec_fg(f"ip netns exec {iface[0]} ip link set {iface[2]} up").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip address add {iface[1]} dev {iface[2]}").returncode

    ret += exec_fg(f"ip netns exec nxp1 sysctl -w net.ipv4.ip_forward=1").returncode
    ret += exec_fg(f"ip netns exec nxp2 sysctl -w net.ipv4.ip_forward=1").returncode
    
    if ret > 0:
        print("Error(s) during interface config")
        return False
    return True


def config_physical() -> bool:
    ret = exec_fg(f"ip netns")
    if ret.stdout != '':
        for ns in namespaces:
            if ns in ret.stdout:
                print(f"Error: {ns} namespace is exists. Maybe run `ip -all netns delete`?")
                return False

    ret = 0
    for ns in namespaces:
        ret += exec_fg(f"ip netns add {ns}").returncode
        ret += exec_fg(f"ip netns exec {ns} sysctl -w net.ipv6.conf.all.disable_ipv6=1").returncode
        ret += exec_fg(f"ip netns exec {ns} ip link set dev lo up").returncode

    ret += exec_fg(f"ip link add teth0 netns talker type veth peer name aeth0 netns nsx").returncode
    ret += exec_fg(f"ip link add leth0 netns listener type veth peer name beth0 netns nsx").returncode

    for iface in nsx_physical_ifaces:
        ret += exec_fg(f"ip link set {iface} netns nsx").returncode
        ret += exec_fg(f"ip netns exec nsx ip link set dev {iface} promisc on").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} gro on").returncode
        ret += exec_fg(f"ip netns exec nsx ip link set dev {iface} up").returncode
        ret += exec_fg(f"ip netns exec nsx ip link set dev {iface} mtu 1500").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -s {iface} autoneg on speed 2500 duplex full").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} rxvlan off txvlan off").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} rx off tx off").returncode

    for iface in nsx_veth_ifaces:
        ret += exec_fg(f"ip netns exec nsx ip link set dev {iface} mtu 1500").returncode
        ret += exec_fg(f"ip netns exec nsx ip link set dev {iface} up").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} gro on").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} rxvlan off txvlan off").returncode
        ret += exec_fg(f"ip netns exec nsx ethtool -K {iface} rx off tx off").returncode

    for iface in tx_lx_ifaces:
        ret += exec_fg(f"ip netns exec {iface[0]} ip link set dev {iface[1]} up").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ethtool -K {iface[1]} gro on").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ethtool -K {iface[1]} rxvlan off txvlan off tx off rx off").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip link add link {iface[1]} name {iface[1]}.10 type vlan id 10").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip link set {iface[1]}.10 address {iface[3]}").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip nei add {iface[5]} dev {iface[1]}.10 lladdr {iface[4]}").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip link set dev {iface[1]}.10 up").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip link set dev {iface[1]} mtu 1490").returncode
        ret += exec_fg(f"ip netns exec {iface[0]} ip addr add {iface[2]} dev {iface[1]}.10").returncode

    if ret > 0:
        print("Error(s) during interface config")
        return False
    return True


def remove_etf(veth):
    if veth:
        exec_fg(f"ip netns exec nxp1 tc qdisc del dev swp0 root")
    else:
        exec_fg(f"ip netns exec nsx tc qdisc del dev {nsx_physical_ifaces[0]} root")
        exec_fg(f"ip netns exec nsx tc qdisc del dev {nsx_physical_ifaces[2]} root")


def setup_etf(veth, delta) -> bool:
    ret = 0
    if veth:
        ret += exec_fg(f"ip netns exec nxp1 tc qdisc add dev {veth_ifaces[2][2]} handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0").returncode
        ret += exec_fg(f"ip netns exec nxp1 tc qdisc replace dev {veth_ifaces[2][2]} parent 100:2 etf clockid CLOCK_TAI delta {delta} skip_sock_check").returncode
    else:
        ret += exec_fg(f"ip netns exec nsx tc qdisc add dev {nsx_physical_ifaces[0]} handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0").returncode
        ret += exec_fg(f"ip netns exec nsx tc qdisc replace dev {nsx_physical_ifaces[0]} parent 100:2 etf clockid CLOCK_TAI delta {delta} skip_sock_check offload").returncode
        ret += exec_fg(f"ip netns exec nsx tc qdisc add dev {nsx_physical_ifaces[2]} handle 100: parent root mqprio num_tc 3 map 0 1 2 2 queues 1@0 1@1 2@2 hw 0").returncode
        ret += exec_fg(f"ip netns exec nsx tc qdisc replace dev {nsx_physical_ifaces[2]} parent 100:2 etf clockid CLOCK_TAI delta {delta} skip_sock_check offload").returncode

    if ret > 0:
        print("Error(s) during ETF config")
        return False
    return True


def run_ptp4l():
    exec_bg(f"ip netns exec nsx ptp4l -i {nsx_physical_ifaces[0]} -p /dev/ptp4 -i {nsx_physical_ifaces[1]} -p /dev/ptp4 -i {nsx_physical_ifaces[2]} -p /dev/ptp4 -i {nsx_physical_ifaces[3]} -p /dev/ptp4 -m")
    for iface in nsx_physical_ifaces:
        exec_bg(f"ip netns exec nsx phc2sys -rr -m -R 10 -c {iface} -s CLOCK_REALTIME -O 0 -z /var/run/ptp4l -w")

def stop_ptp4l():
    exec_fg(f"killall ptp4l")
    exec_fg(f"killall phc2sys")


def cleanup():
    print("\nCleaning up...")
    stop_ptp4l()
    stop_R2DTWO()
    for ns in namespaces:
        exec_fg(f"ip netns del {ns}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("-p", "--physical", help="run tests on physical NICs", action="store_true")
    args = parser.parse_args()
    
    global namespaces
    passed = 0
    all = 0

    try:
        print("R2DTWO delay offload tests:")
        if args.physical:
            # Physical tests
            namespaces = ['talker', 'listener', 'nsx']
            if not config_physical():
                cleanup()
                return

            print(f"Physical tests:")
            run_ptp4l()
            for ping_type in PING_TYPES:
                for delta in DELTAS:
                    if not setup_etf(veth=False, delta=delta):
                        cleanup()
                        return
                    result = run_offload_physical_tests(ping_type, delta / NSEC_PER_MSEC)
                    passed += result
                    all += 1
                    print(f"{'✔' if result else '✘'}")
                    remove_etf(veth=False)
            cleanup()
        else:
            # Veth tests
            namespaces = ['talker', 'listener', 'nxp1', 'nxp2']
            if not config_veth():
                cleanup()
                return
            
            for conf in config:
                print(f"{conf.capitalize()} tests:")
                for proto in PROTO:
                    start_R2DTWO(proto, conf)
                    for ping_type in PING_TYPES:
                        for delta in DELTAS:
                            if not setup_etf(veth=True, delta=delta):
                                cleanup()
                                return
                            result = run_offload_veth_tests(proto, conf, ping_type, delta / NSEC_PER_MSEC)
                            passed += result
                            all += 1
                            print(f"{'✔' if result else '✘'}")
                            remove_etf(veth=True)
                    stop_R2DTWO()
            cleanup()

        if passed == all:
            print(f'All tests completed, {passed}/{all} successfully')
        else:
            print(f'Some tests completed, {passed}/{all} successfully')

    except KeyboardInterrupt:
        print("\nInterrupted")
        cleanup()
    except BaseException as e:
        print(e)

if __name__ == "__main__":
    main()
