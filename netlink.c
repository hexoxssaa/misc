#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <linux/if.h>
#include <linux/if_addr.h>
#include <arpa/inet.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <unistd.h>

#define BUFFER_SIZE 8192

int mngtmp = 0;

static inline __u32 rta_getattr_u32(const struct rtattr *rta)
{
	return *(__u32 *)RTA_DATA(rta);
}

static unsigned int get_ifa_flags(struct ifaddrmsg *ifa,
    struct rtattr *ifa_flags_attr)
{
return ifa_flags_attr ? rta_getattr_u32(ifa_flags_attr) :
ifa->ifa_flags;
}

int parse_rtattr_flags(struct rtattr *tb[], int max, struct rtattr *rta, int len, unsigned short flags)
{
	unsigned short type;

	memset(tb, 0, sizeof(struct rtattr *) * (max + 1));
	while (RTA_OK(rta, len)) {
		type = rta->rta_type & ~flags;
		if ((type <= max) && (!tb[type]))
			tb[type] = rta;
		rta = RTA_NEXT(rta, len);
	}
	if (len)
		fprintf(stderr, "!!!Deficit %d, rta_len=%d\n",
			len, rta->rta_len);
	return 0;
}

int parse_rtattr(struct rtattr *tb[], int max, struct rtattr *rta, int len)
{
	return parse_rtattr_flags(tb, max, rta, len, 0);
}


// 处理接收到的RTNetlink消息
void handle_rtnl_message(struct nlmsghdr *nlh) {
    struct ifaddrmsg *ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    struct rtattr *rta = IFA_RTA(ifa);
    int rtl = IFA_PAYLOAD(nlh);

    unsigned int ifa_flags;
	struct rtattr *rta_tb[IFA_MAX+1];

    parse_rtattr(rta_tb, IFA_MAX, IFA_RTA(ifa),nlh->nlmsg_len - NLMSG_LENGTH(sizeof(*ifa)));

    ifa_flags = get_ifa_flags(ifa, rta_tb[IFA_FLAGS]);


    // 只处理IPv6地址
    int cmprslt = 0;
    if (mngtmp == 1){
        cmprslt = ifa->ifa_family == AF_INET6 && ifa_flags & IFA_F_MANAGETEMPADDR;
    }else{
        cmprslt = ifa->ifa_family == AF_INET6;
    }
    if (cmprslt == 1) {
        char ifname[IFNAMSIZ];
        if_indextoname(ifa->ifa_index, ifname);

        // 遍历消息中的属性
        for (; RTA_OK(rta, rtl); rta = RTA_NEXT(rta, rtl)) {
            if (rta->rta_type == IFA_LOCAL || rta->rta_type == IFA_ADDRESS) { 
                //printf("rta_type:%x ", rta->rta_type);
                struct in6_addr *addr = (struct in6_addr *)RTA_DATA(rta);
                char addr_str[INET6_ADDRSTRLEN];
                if ((addr->s6_addr[0] & 0xE0) == 0x20){
                    inet_ntop(AF_INET6, addr, addr_str, INET6_ADDRSTRLEN);
                    //printf("Interface: %s, IPv6 Address: %s\n", ifname, addr_str);
                    printf("%s\n", addr_str);
                }
            }
        }
    }
}

int main(int argc, char *argv[]) {
    int sockfd;
    struct sockaddr_nl nladdr;
    struct nlmsghdr *nlh;
    struct ifaddrmsg *ifa;
    char buffer[BUFFER_SIZE];

    int i;
    // 遍历所有命令行参数
    for (i = 1; i < argc; i++) {
        // 检查当前参数是否为 -m
        if (strcmp(argv[i], "-m") == 0) {
            mngtmp = 1;
        }
    }

    // 创建Netlink套接字
    sockfd = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sockfd < 0) {
        perror("socket");
        return 1;
    }

    // 初始化Netlink地址
    memset(&nladdr, 0, sizeof(nladdr));
    nladdr.nl_family = AF_NETLINK;
    nladdr.nl_pid = getpid();
    nladdr.nl_groups = 0;

    // 绑定套接字
    if (bind(sockfd, (struct sockaddr *)&nladdr, sizeof(nladdr)) < 0) {
        perror("bind");
        close(sockfd);
        return 1;
    }

    // 初始化Netlink消息头
    nlh = (struct nlmsghdr *)buffer;
    nlh->nlmsg_len = NLMSG_LENGTH(sizeof(struct ifaddrmsg));
    nlh->nlmsg_type = RTM_GETADDR;
    nlh->nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    nlh->nlmsg_seq = 1;
    nlh->nlmsg_pid = getpid();

    // 初始化接口地址消息
    ifa = (struct ifaddrmsg *)NLMSG_DATA(nlh);
    ifa->ifa_family = AF_INET6;
    ifa->ifa_prefixlen = 0;
    ifa->ifa_flags = 0;
    ifa->ifa_scope = 0;
    ifa->ifa_index = 0;

    // 发送Netlink消息
    if (send(sockfd, buffer, nlh->nlmsg_len, 0) < 0) {
        perror("send");
        close(sockfd);
        return 1;
    }

    // 接收Netlink消息
    while (1) {
        ssize_t len = recv(sockfd, buffer, BUFFER_SIZE, 0);
        if (len < 0) {
            perror("recv");
            break;
        }

        for (nlh = (struct nlmsghdr *)buffer; NLMSG_OK(nlh, len); nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE) {
                break;
            }
            handle_rtnl_message(nlh);
        }

        if (nlh->nlmsg_type == NLMSG_DONE) {
            break;
        }
    }

    // 关闭套接字
    close(sockfd);

    return 0;
}
