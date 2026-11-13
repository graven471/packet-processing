#include "dpdk.h"
#include <asm-generic/errno-base.h>
#include <rte_common.h>
#include <rte_config.h>
#include <rte_debug.h>
#include <rte_dev.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_lcore_var.h>
#include <rte_log.h>
#include <rte_malloc.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_per_lcore.h>
#include <rte_ring.h>
#include <rte_ring_core.h>
#include <stdint.h>
#include <sys/types.h>

/*
 * core components of DPDK
 * 1. rte_timer : Timer facilities. Based on HPET interface that is provided by
 * EAL
 * 2. rte_malloc : Allocation of named memory zones using libc's malloc
 * 3. rte_mempool : Handle a pool of objects using ring to store them. Allow
 * bulk enqueue/dequeue and per-CPU cache.
 * 4. rte_eal + libc : Environment abstraction layer: RTE loading, memory
 * allocation, PCI access, logging etc
 * 5. rte_mbuf : Manipulation of packet buffers carrying network data
 * 6. rte_ring : Fixed-size lockless FIFO for storing objects in table
 * 7. rte_debug : Provides debug helpers
 */

int init_eal(int argc, char **argv)
{
	int retval = rte_eal_init(argc, argv);

	if (retval == EAGAIN)
		retval = rte_eal_init(argc, argv);

	if (retval < -1)
		return -retval;

	return 0;
}

// eal must needs to be initialized before calling this function
int configure_port(struct dpdk_port_config *_Nonnull config)
{
	/*
     * core idea:
     *  DPDK Poll Mode Drivers (PMDs) are high-performance, optimized drivers for various network interface cards
     *  that bypass the traditional kernel network stack to reduce latency and improve throughput.
     *  They access Rx and Tx descriptors directly in a polling mode without relying
     *  on interrupts (except for Link Status Change notifications), enabling efficient packet
     *  reception and transmission in user-space applications.
     */

	/*
	 * There is a two model for a packet processing simplified idea:
	 * 1. run-to-completion model - everything happens on same lcore, receiving, processing and sending
	 * 2. pipeline model - docs says:  one core polls the Rx descriptor ring(s) of one or more ports via an API.
	 *    The application then passes received packets to another core via a ring for further processing,
	 *    which may include transmission through the Tx descriptor ring using an API.

      * In a synchronous run-to-completion model, a logical core (lcore)
      * assigned to DPDK executes a packet processing loop that includes the following steps:
      * Retrieve input packets using the PMD receive API
      * Process each received packet individually, up to its forwarding
      * Transmit output packets using the PMD transmit API

      * In contrast, the asynchronous pipeline model assigns some logical cores to retrieve packets
      * and others to process them. The application exchanges packets between cores via rings.
	 */

	// (notes): so there is "port level" and "queue level" in DPDK

	int retval;
	struct rte_eth_dev_info dev_info = { 0 };
	struct rte_eth_conf port_conf = { 0 };

	if (config->rx_mempool == nullptr)
		return -EINVAL;

	if (!rte_eth_dev_is_valid_port(config->port_id))
		return -EINVAL;

	retval = rte_eth_dev_info_get(config->port_id, &dev_info);
	if (retval != 0)
		return retval;

	RTE_LOG(INFO, USER1, "Driver: %s, max_rx_queues: %d, max_tx_queues: %d, max_rx_mempools: %d, max_mtu = %d\n",
		dev_info.driver_name, dev_info.max_rx_queues, dev_info.max_tx_queues, dev_info.max_rx_mempools,
		dev_info.max_mtu);

#define TX_OFFLOAD_SUPPORTED(flag, rte_flag) ((config->tx_flags & (flag)) && (dev_info.tx_offload_capa & (rte_flag)))
#define RX_OFFLOAD_SUPPORTED(flag, rte_flag) ((config->rx_flags & (flag)) && (dev_info.rx_offload_capa & (rte_flag)))

	// this is kinda reminding me of VkPhysicalDeviceFeatures :)
	if (TX_OFFLOAD_SUPPORTED(TX_OFFLOAD_VLAN_INSERT, RTE_ETH_TX_OFFLOAD_VLAN_INSERT)) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_VLAN_INSERT;
		RTE_LOG(INFO, USER1, "Enabling TX_OFFLOAD_VLAN_INSERT feature\n");
	}

	if (TX_OFFLOAD_SUPPORTED(TX_OFFLOAD_UDP_TSO, RTE_ETH_TX_OFFLOAD_UDP_TSO)) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_UDP_TSO;
		RTE_LOG(INFO, USER1, "Enabling TX_OFFLOAD_UDP_TSO feature\n");
	}

	if (TX_OFFLOAD_SUPPORTED(TX_OFFLOAD_IPV4_CKSUM, RTE_ETH_TX_OFFLOAD_IPV4_CKSUM)) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_IPV4_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling TX_OFFLOAD_IPV4_CKSUM feature\n");
	}

	if (TX_OFFLOAD_SUPPORTED(TX_OFFLOAD_UDP_CKSUM, RTE_ETH_TX_OFFLOAD_UDP_CKSUM)) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_UDP_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling TX_OFFLOAD_UDP_CKSUM feature\n");
	}

	if (TX_OFFLOAD_SUPPORTED(TX_OFFLOAD_TCP_CKSUM, RTE_ETH_TX_OFFLOAD_TCP_CKSUM)) {
		port_conf.txmode.offloads |= RTE_ETH_TX_OFFLOAD_TCP_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling TX_OFFLOAD_TCP_CKSUM feature\n");
	}

	if (RX_OFFLOAD_SUPPORTED(RX_OFFLOAD_IPV4_CKSUM, RTE_ETH_RX_OFFLOAD_IPV4_CKSUM)) {
		port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TCP_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling RX_OFFLOAD_IPV4_CKSUM feature\n");
	}

	if (RX_OFFLOAD_SUPPORTED(RX_OFFLOAD_TCP_CKSUM, RTE_ETH_RX_OFFLOAD_TCP_CKSUM)) {
		port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_TCP_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling RX_OFFLOAD_TCP_CKSUM feature\n");
	}

	if (RX_OFFLOAD_SUPPORTED(RX_OFFLOAD_UDP_CKSUM, RTE_ETH_RX_OFFLOAD_UDP_CKSUM)) {
		port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_UDP_CKSUM;
		RTE_LOG(INFO, USER1, "Enabling RX_OFFLOAD_UDP_CKSUM feature\n");
	}

	if (RX_OFFLOAD_SUPPORTED(RX_OFFLOAD_VLAN_STRIP, RTE_ETH_RX_OFFLOAD_VLAN_STRIP)) {
		port_conf.rxmode.offloads |= RTE_ETH_RX_OFFLOAD_VLAN_STRIP;
		RTE_LOG(INFO, USER1, "Enabling RX_OFFLOAD_VLAN_STRIP feature\n");
	}

	if (config->nb_rx_queues > dev_info.max_rx_queues) {
		RTE_LOG(WARNING, USER1, "Requested RX queues (%u) exceeds maximum (%u). Clamping.\n",
			config->nb_rx_queues, dev_info.max_rx_queues);
		config->nb_rx_queues = dev_info.max_rx_queues;
	}

	if (config->nb_tx_queues > dev_info.max_tx_queues) {
		RTE_LOG(WARNING, USER1, "Requested TX queues (%u) exceeds maximum (%u). Clamping.\n",
			config->nb_tx_queues, dev_info.max_tx_queues);
		config->nb_tx_queues = dev_info.max_tx_queues;
	}

	// step 1 configure
	retval = rte_eth_dev_configure(config->port_id, config->nb_rx_queues, config->nb_tx_queues, &port_conf);
	if (retval < 0)
		return retval;

	// this already clamps and align descriptors we don't need to do anything
	retval = rte_eth_dev_adjust_nb_rx_tx_desc(config->port_id, &config->nb_rx_descriptors,
						  &config->nb_tx_descriptors);
	if (retval != 0)
		return retval;

	// step 2 setup tx queue
	for (uint16_t q = 0; q < config->nb_tx_queues; ++q) {
		retval = rte_eth_tx_queue_setup(config->port_id, q, config->nb_tx_descriptors,
						(unsigned int)rte_eth_dev_socket_id(config->port_id),
						&dev_info.default_txconf);
		if (retval != 0)
			return retval;
	}

	// step 3 setup rx queue
	for (uint16_t q = 0; q < config->nb_rx_queues; ++q) {
		// (todo): investigate about Multi-segment RX (buffer split)
		retval = rte_eth_rx_queue_setup(config->port_id, q, config->nb_rx_descriptors,
						(unsigned int)rte_eth_dev_socket_id(config->port_id),
						&dev_info.default_rxconf, config->rx_mempool);
		if (retval != 0)
			return retval;
	};

	// step 4 start the Ethernet port
	retval = rte_eth_dev_start(config->port_id);
	if (retval != 0)
		return retval;

	struct rte_ether_addr addr = { 0 };
	retval = rte_eth_macaddr_get(config->port_id, &addr);
	if (retval != 0)
		return retval;

	RTE_LOG(INFO, USER1,
		"Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
		config->port_id, RTE_ETHER_ADDR_BYTES(&addr));

	rte_delay_ms(2000);

	struct rte_eth_link link = { 0 };

	int ret = rte_eth_link_get_nowait(config->port_id, &link);
	if (ret < 0) {
		printf("Link query failed: %d\n", ret);
		return -1;
	}

	printf("Port %u: ", config->port_id);

	if (link.link_status == RTE_ETH_LINK_UP) {
		printf("UP | %" PRIu32 " Mbps | %s duplex | %s\n", link.link_speed,
		       link.link_duplex == RTE_ETH_LINK_FULL_DUPLEX ? "Full" : "Half",
		       link.link_autoneg == RTE_ETH_LINK_AUTONEG ? "Auto-negotiation" : "Fixed");
	} else {
		printf("DOWN\n");
	}

	RTE_LOG(INFO, USER1, "port init success\n");
	return 0;
}
