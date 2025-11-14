from gcc
RUN apt -qq -y update && \
  apt -qq -y install --no-install-recommends \
  cmake \
  mininet \
  python3-pyroute2 \
  python3-regex \
  python3-scapy \
  iproute2 \
  strace  \
  psmisc \
  iptables \
  iputils-ping \
  valgrind \
  tshark \
  clang && \
  echo y | cpan YAML::Tiny
