/* SPDX-License-Identifier: BSD-3-Clause
 * Copyright(c) 2010-2015 Intel Corporation
 */

#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>
#include <unistd.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32
#define PKT_LENGTH 128

struct rte_mempool *mbuf_pool;

/* Main functional part of port initialization. 8< */
static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	const uint16_t rx_rings = 1, tx_rings = 1;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_conf port_conf = {
		.txmode = {
			.offloads =
				RTE_ETH_TX_OFFLOAD_IPV4_CKSUM  |
				RTE_ETH_TX_OFFLOAD_UDP_CKSUM   |
		},
	};
	struct rte_eth_txconf txconf;
	struct rte_eth_rxconf rxconf;
	struct rte_eth_dev_info dev_info;

	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) {
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	printf("RTE_ETH_TX_OFFLOAD_IPV4_CKSUM = %lu\n", dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_IPV4_CKSUM);
	printf("RTE_ETH_TX_OFFLOAD_UDP_CKSUM = %lu\n", dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_UDP_CKSUM);
	
	port_conf.txmode.offloads &= dev_info.tx_offload_capa;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	rxconf = dev_info.default_rxconf;
	rxconf.offloads = port_conf.rxmode.offloads;
	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) {
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), &rxconf, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) {
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}
/* >8 End of main functional part of port initialization. */

static inline uint32_t 
csum32_add(uint32_t a, uint32_t b) {
	a += b;
	return a + (a < b);
}

static inline uint16_t 
csum16_add(uint16_t a, uint16_t b) {
	a += b;
	return a + (a < b);
}

static inline void
init_udp_packet(struct rte_mbuf *m) {
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *iph;
	struct rte_udp_hdr *udph;
	char *l4_payload;
	uint32_t l4_payload_len;

	m->pkt_len = PKT_LENGTH;
	m->data_len = PKT_LENGTH;

	// ether hdr 14byte
	eth = rte_pktmbuf_mtod(m, struct rte_ether_hdr *);
	// akiyama32 PORT 0 addr
	rte_ether_unformat_addr("a0:36:9f:3f:20:24", &eth->dst_addr);
	// localhost PORT 0 addr
	rte_ether_unformat_addr("a0:36:9f:53:9e:1c", &eth->src_addr);
	eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

	// ipv4 hdr 20byte
	iph = (struct rte_ipv4_hdr*)(eth + 1);
	iph->version = 4;
	iph->ihl = sizeof(struct rte_ipv4_hdr) / 4;
	iph->type_of_service = 0;
	iph->total_length = rte_cpu_to_be_16(PKT_LENGTH
											- sizeof(struct rte_ether_hdr));
	iph->packet_id = 0;
	iph->fragment_offset = 0;
	iph->time_to_live = 64;
	iph->next_proto_id = IPPROTO_UDP;
	iph->hdr_checksum = 0;
	//iph->hdr_checksum = rte_ipv4_cksum(iph);
	iph->src_addr = RTE_IPV4(31, 0, 168, 192);
	iph->dst_addr = RTE_IPV4(32, 0, 168, 192);

	// udp hdr 8byte
	udph = (struct rte_udp_hdr*)(iph + 1);
	udph->src_port = rte_cpu_to_be_16(1000);
	udph->dst_port = rte_cpu_to_be_16(2000);
	udph->dgram_len = rte_cpu_to_be_16(PKT_LENGTH 
										- sizeof(struct rte_ether_hdr)
										- sizeof(struct rte_ipv4_hdr));
	udph->dgram_cksum = 0;

	// udp payload
	l4_payload = (char*)(udph + 1);
	l4_payload_len = PKT_LENGTH
						- sizeof(struct rte_ether_hdr)
						- sizeof(struct rte_ipv4_hdr)
						- sizeof(struct rte_udp_hdr);
	char str[] = "1234567890";
	for (int i = 0; i < l4_payload_len; i++)
		l4_payload[i] = str[i % sizeof(str)];

	/* Must be set to offload checksum. */
	m->l2_len = sizeof(struct rte_ether_hdr);
	m->l3_len = sizeof(struct rte_ipv4_hdr);

	/* Enable IPV4 CHECKSUM OFFLOAD */
	m->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM);

	/* Enable UDP TX CHECKSUM OFFLOAD */
	m->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_UDP_CKSUM);
	uint32_t pseudo_cksum = csum32_add(
		csum32_add(iph->src_addr, iph->dst_addr),
		(iph->next_proto_id << 24) + udph->dgram_len
	);
	udph->dgram_cksum = csum16_add(pseudo_cksum & 0xFFFF, pseudo_cksum >> 16);
}

static __rte_noreturn void
lcore_main(void)
{
	uint16_t port;
	struct rte_mbuf *bufs[BURST_SIZE];
	struct rte_ether_hdr *eth;
	struct rte_ipv4_hdr *iph;
	struct rte_udp_hdr *udph;
	char *l4_payload;
	uint32_t l4_payload_len;

	/* Main work of application loop. */
	for (int j = 0; ; j++) {
		if (rte_pktmbuf_alloc_bulk(mbuf_pool, bufs, BURST_SIZE)) {
			printf("failed to alloc.\n");
		}

		for (int i = 0; i < BURST_SIZE; i++) {
			init_udp_packet(bufs[i]);
		}

		/* Send burst of TX packets, to second port of pair. */
		const uint16_t nb_tx = rte_eth_tx_burst(1, 0, bufs, BURST_SIZE);
		if (nb_tx)
			printf("nb_tx = %d, j = %d\n", nb_tx, j);

		/* Free any unsent packets. */
		if (unlikely(nb_tx < BURST_SIZE)) {
			rte_pktmbuf_free_bulk(&bufs[nb_tx], BURST_SIZE - nb_tx);
		}

		sleep(1);
	}
	/* >8 End of loop. */
}


int
main(int argc, char *argv[])
{
	unsigned nb_ports;
	uint16_t portid;

	/* Initializion the Environment Abstraction Layer (EAL). */
	int ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* End of initialization the Environment Abstraction Layer (EAL). */

	argc -= ret;
	argv += ret;

	/* Check that there is an even number of ports to send/receive on. */
	nb_ports = rte_eth_dev_count_avail();
	else if (nb_ports == 0)
		rte_exit(EXIT_FAILURE, "Cannot find avail port.\n");

	/* Creates a new mempool in memory to hold the mbufs. */

	/* Allocates mempool to hold the mbufs. */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. */
	RTE_ETH_FOREACH_DEV(portid)
		if (port_init(portid, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n",
					portid);
	/* End of initializing all ports. */

	if (nb_ports > 1)
		printf("\nWARNING: Too many ports enabled. Only 1 used.\n");

	if (rte_lcore_count() > 1)
		printf("\nWARNING: Too many lcores enabled. Only 1 used.\n");

	/* Call lcore_main on the main core only. Called on single lcore. */
	lcore_main();
	/* End of called on single lcore. */

	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
