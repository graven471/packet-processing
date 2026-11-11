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

	RTE_LOG(INFO, USER1, "port init success\n");
	return 0;
}

[[clang::unavailable("notes")]] static inline void ixgbe_datasheet_notes(void)
{
	/*
     * this is my personal notes i'm learning NIC reference 82599 datasheet
     *
     * (todo): read Section 7.2.6 (Transmit Descriptor Fetching)
     *
     * Trasmit (Tx) : when we send ethernet packets over wire
     * Receive (Rx) : when we receive ethernet packets over wire
     *
     * Section 1.9 Architecture and basic operation
     * 1.9.1 Transmit (Tx) Data Flow
     * def: Tx data flow provides a high-level description of all data/control transformations
     *      steps needed for sending Ethernet packets over wire
     *
     * "host" means our Computer
     *
     * Tx Data flow
     * 1. the host creates a descriptor ring and configures one of the the 82599's transmission queues
     * with the address location, length, head, and tail pointers of the ring (one of 128 available Tx queues).
     *
     * 2. The host is requested by the TCP/IP stack to transmit a packet,
     *  it gets the packet data within one or more data buffers. (kernel networking stack in our case DPDK's)
     * "data buffer(s)" must be at DMA compatiable address so that NIC can see it and fetch it
     *
     * void *packet = rte_pktmbuf_mtod(m, void *); this is cpu virtual address
     * rte_iova_t iova = rte_mbuf_data_iova(m); this is DMA address
     *
     * 3. The host initializes the descriptor(s) that points to the data buffer(s) and have additional control
     * parameters that describes the needed hardware functionally. The host places that descriptor in the
     * correct location at the appropiate Tx ring.
     *
     * this is important step:
     *
     * so what is Tx descriptors ? conceputally it looks like this not 82599 descriptor btw
     * struct tx_descriptor {
         uint64_t buffer_address;  // DMA address
         uint32_t length;
         uint32_t command;
     };
     * `buffer_address` : its a physical address not virtual it tells the NIC where to find packet data
     * this step is preparing for DMA not actually doing DMA
     *
     * 4. The host updates the appropiate Queue Tail Pointer (TDT). conceptually this is like Nvme doorbell
     *
     * 5. The 82599’s DMA senses a change of a specific TDT and as a result sends a PCIe request to fetch the descriptor(s)
     *     from host memory.
     *
     * 6. The descriptor(s) content is received in a PCIe read completion and is written to the appropriate location in the
     *    descriptor queue.
     *
     * for example let's say it fetched something like:
     * TX Descriptor (internal NIC storage)
     * buffer_addr = 0x90000000
     * length      = 1514
     * command     = EOP | IFCS
     *
     * packet data starts at address `0x90000000`
     * packet length is 1514 bytes
     * EOP indicates the end of the packet
     * ICFS requests Ethernet FCS generation
     *
     * 7. The DMA fetches the next descriptor and processes its content. As a result, the DMA sends PCIe requests to fetch
     *    the packet data from system memory.
     *
     * remember this like even in dreams "CPU never copies packet into the NIC. The NIC pulls the packet directly"
     * it fetches the packet at 0x90000000 copies from Main Memory to NIC internal storage
     *
     * 8. The packet data is being received from PCIe completions and passes through the transmit DMA that performs all
     *    programmed data manipulations (various CPU offloading tasks as checksum offload, TSO offload, etc.) on the
     *    packet data on the fly.
     *
     * 9. While the packet is passing through the DMA, it is stored into the transmit FIFO.
     *    After the entire packet is stored in the transmit FIFO, it is then forwarded to transmit switch module
     *
     * 10. The transmit switch arbitrates between host and management packets and eventually forwards the packet to the MAC.
     *
     * 11. The MAC appends the L2 CRC to the packet and sends the packet over the wire using a pre-configured interface.
     *
     * 12. When all the PCIe completions for a given packet are complete, the DMA updates the appropriate descriptor(s).
     *
     * 13. The descriptors are written back to host memory using PCIe posted writes. The head pointer is updated in host
     *     memory as well
     *
     * 14. An interrupt is generated to notify the host driver that the specific packet has been read to the 82599 and the
     *     driver can then release the buffer(s).
     *
     * in my own words:
     * 1. during init create a descriptor ring pick head, tail and length etc
     * 2. TCP/IP requested us to transmit the packet we take the packet into one or more data buffers and data buffers
     *    must be DMA accessible address
     * 3. we take a Tx Descriptor fill it pointing data buffer address length and control flags
     * 4. we update the tail pointer of the ring conceptually same as NVME doorbell
     * 5. 82599 sense ohh something is changed i got new descriptor it sends a req through PCIe i want that descriptor content
     * 6. content received
     * 7. then it reads the data_address and does a DMA requests to fetch the packet data
     * 8. it does data processing checksum etc
     * 9. updates MAC address and all not relevant
     * 10. then since NIC is the consumer it updates the head pointer descriptors are written back to host memory
     *     using PCIe posted writes
     * 11. then it generates interrupts packet has been now we can free the data buffers and can reuse descriptor
     *
     * 1.9.2    Receive (Rx) Data Flow
     * def: Rx data flow provides high-level description of all data/control transformation steps needed for
     *      receiving Ethernet packets.
     *
     * 1. The host creates a descriptor ring and configures one of the 82599’s receive queues with the address location,
     *     length, head, and tail pointers of the ring (one of 128 available Rx queues)
     *
     * 2. The host initializes descriptor(s) that point to empty data buffer(s). The host places these descriptor(s) in the
     *    correct location at the appropriate Rx ring.
     *
     * 3. The host updates the appropriate Queue Tail Pointer (RDT).
     *
     * 6. A packet enters the Rx MAC.
     *
     * 7. The MAC forwards the packet to the Rx filter.
     *
     * 8. If the packet matches the pre-programmed criteria of the Rx filtering, it is forwarded to an Rx FIFO.
     *
     * 9. The receive DMA fetches the next descriptor from the appropriate host memory ring to be used for the next
     *    received packet.
     *
     * 10. After the entire packet is placed into an Rx FIFO, the receive DMA posts the packet data to the location indicated by
     *     the descriptor through the PCIe interface.
     * If the packet size is greater than the buffer size, more descriptors are fetched and their buffers are used for the
     * received packet
     *
     * 11. When the packet is placed into host memory, the receive DMA updates all the descriptor(s) that were used by the
     *     packet data
     *
     * 12. The receive DMA writes back the descriptor content along with status bits that indicate the packet information
     *      including what offloads were done on that packet
     *
     * 13. The 82599 initiates an interrupt to the host to indicate that a new received packet is ready in host memory.
     *
     * 14. The host reads the packet data and sends it to the TCP/IP stack for further processing. The host releases the
     *     associated buffer(s) and descriptor(s) once they are no longer in use.
     */
}
