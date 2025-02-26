echo "net.ipv4.tcp_rmem = 16384 4194304 536870912" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.tcp_wmem = 16384 4194304 536870912" | sudo tee -a /etc/sysctl.conf
echo "net.core.somaxconn = 65535" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.tcp_tw_reuse = 1" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.ip_local_port_range = 1024 65535" | sudo tee -a /etc/sysctl.conf
echo "net.ipv4.tcp_fin_timeout = 1" | sudo tee -a /etc/sysctl.conf
echo "fs.file-max = 2097152" | sudo tee -a /etc/sysctl.conf

echo "* soft nofile 1048576" | sudo tee -a /etc/security/limits.conf
echo "* hard nofile 1048576" | sudo tee -a /etc/security/limits.conf
echo "root soft nofile 1048576" | sudo tee -a /etc/security/limits.conf
echo "root hard nofile 1048576" | sudo tee -a /etc/security/limits.conf

sudo sysctl --system