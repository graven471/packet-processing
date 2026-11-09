#include "dpdk.h"
#include <rte_common.h>
#include <rte_debug.h>
#include <rte_dev.h>
#include <rte_eal.h>
#include <rte_errno.h>
#include <rte_ethdev.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_lcore_var.h>
#include <rte_log.h>
#include <rte_mbuf.h>
#include <rte_mbuf_core.h>
#include <rte_memory.h>
#include <rte_mempool.h>
#include <rte_per_lcore.h>
#include <stdio.h>
#include <stdlib.h>

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

static inline void init_eal()
{
	char *argv[] = {
		"packet-processor",
		"-l",
		"0-10", // lcores 0 to 10
		"-n",
		"4", // memory channels
	   // "--no-huge", // for dev/testing without hugepages
	};
	int argc = sizeof(argv) / sizeof(argv[0]);

	int ret;
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_panic("Cannot init EAL\n");

	RTE_LOG(INFO, USER1, "EAL initialization success\n");
}

struct lcore_state {
	int a;
	int b;
};

// this lcore_states points to the starting mem_address where struct lcore_state
// is allocated each CPU have its own data no sharing
static RTE_LCORE_VAR_HANDLE(struct lcore_state, lcore_states);
RTE_LCORE_VAR_INIT(lcore_states);

RTE_INIT(rte_state_init)
{
	RTE_LCORE_VAR_ALLOC(lcore_states);

	unsigned int lcore_id;
	struct lcore_state *state;
	RTE_LCORE_VAR_FOREACH(lcore_id, state, lcore_states)
	{
		state->a = 20;
		state->b = 30;
	}
	rte_eal_wait_lcore(lcore_id);
}

struct foo {
	const char *name;
	uint32_t id;
};

// =============== chapter 2 Memory pool libraries ============
static inline void mem_pool_lib()
{
	// a memory pool is an allocator of a fixed-size object (conceptually same as
	// slab) In the DPDK, it is identified by name and uses a mempool handler to
	// store free objects. default mempool handler is a ring based.

	// memory pool’s ring of free buffers is MPMC it requires CAS operation to
	// access it DPDK docs says: To avoid having too many access requests to the
	// memory pool’s ring, the memory pool allocator can maintain a per-core cache
	// and do bulk requests to the memory pool’s ring, via the cache with many
	// fewer locks on the actual memory pool structure. In this way, each core has
	// full access to its own cache (without locks) of free objects and only when
	// the cache fills does the core need to shuffle some of the free objects back
	// to the pools ring or obtain more objects when the cache is empty
	//
	// here is how i think it works:
	// consider ring of free buffers is a power of two ring no wrapping
	// initially we got a page from mmap say base = 0x1000 and size = 4096
	// our object_size is 32 so the formula is this:
	// B(i) = base + i * object_size, where i is the slot index
	// so we did this and fill up our ring buffer concepually it looks something
	// like this: Object 0  -> 0x1000 Object 1  -> 0x1020 Object 2  -> 0x1040
	// Object 3  -> 0x1060
	// ...
	// Object 127 -> 0x1FE0
	// now instead of fetching a free block from this ring everything during init
	// we can store say 20 object ptr(s) in each CPU cache so, CPU0 stores
	// object0..19, CPU1 stores object 20..39 etc and during allocation we check
	// if cache is not empty then just return from cache no lock no CAS nothing
	// otherwise do CAS and bulk dequeue from shared ring into target cache.
	// that's the idea if i get it correctly

	struct rte_mempool *first_pool =
		rte_mempool_create("test",   /* name of the pool */
				   256,	 /* number of elements in the mempool */
				   sizeof(struct foo), /* The size of each element.  */
				   32, /* cache_size thing i described above each CPU will have 32 blocks
              in there private cache this avoids CAS locks and accessing shared
              ring for each allocation */
				   0,  /* size of the private data (todo): learn about this */
				   nullptr, /* A function pointer that is called for initialization of the
                  pool, before object initialization. */
				   nullptr, /* An opaque pointer to data that can be used in the mempool
                  constructor function */
				   nullptr, /* A function pointer that is called for each object at
                  initialization of the pool. The user can set some meta data in
                  objects if needed */
				   nullptr, /* An opaque pointer to data that can be used as an argument for
                  each call to the object constructor function.  */
				   (int)rte_socket_id(),   /* The socket_id argument is the socket identifier
                          in the case of NUMA */
				   RTE_MEMPOOL_F_NO_SPREAD /* The flags arguments is an OR of following
                                 flags: RTE_MEMPOOL_F_NO_SPREAD,
                                 RTE_MEMPOOL_F_NO_CACHE_ALIGN,
                                 RTE_MEMPOOL_F_SP_PUT, RTE_MEMPOOL_F_SC_GET,
                                 RTE_MEMPOOL_F_NO_IOVA_CONTIG */
		);

	if (first_pool == nullptr) {
		rte_exit(EXIT_FAILURE, "mempool create failed: %s\n", rte_strerror(rte_errno));
	}

	RTE_LOG(INFO, USER1, "Mempool created successfully %p\n", first_pool);

	// now we can access individual objects
	struct foo *obj;

	if (rte_mempool_get(first_pool, (void **)&obj) == 0) {
		obj->name = "hey";
		obj->id = 20;

		// return the object to pool
		rte_mempool_put(first_pool, obj);
	}

	// we can get bulk objects like this
	constexpr unsigned int max_object_count = 20;

	struct foo *objects[max_object_count];

	if (rte_mempool_get_bulk(first_pool, (void **)objects, max_object_count) == 0) {
		for (unsigned i = 0; i < max_object_count; i++) {
			objects[i]->name = "foo";
			objects[i]->id = 40 * i;

			rte_mempool_put_bulk(first_pool, (void **)objects, max_object_count);
		}
	}

	// free the pool
	rte_mempool_free(first_pool);
	first_pool = nullptr;
}

static inline void mbuf_lib(void)
{
	// struct rte_mbuf represents packet in DPDK rte_mbuf is not a packet itself
	// its a metadata conceptually this is same as sk_buff in kernel
	//  docs says:
	// Message buffers may be used to carry control information, packets, events,
	// and so on between different entities in the system
	// in struct rte_mbuf field `buf_addr` describes Virtual address of the
	// beginning of the data buffer
	// there is another intresting field called `data_off` cause packet does not starts
	// from beginning of the buffer there is some headroom and tailroom actual packet is in
	// between so we do something like data pointer = buf_addr + data_off
	// buf_addr points to the beginning of the bytes not actual packet starting addr

	struct rte_mempool *foo_pool = rte_mempool_create("foo_packet", 256, sizeof(struct foo), 32, 0, nullptr,
							  nullptr, nullptr, nullptr, (int)rte_socket_id(),
							  RTE_MEMPOOL_F_NO_SPREAD);

	if (foo_pool == nullptr) {
		rte_exit(EXIT_FAILURE, "mempool create failed: %s\n", rte_strerror(rte_errno));
	}

	// this allocates a new mbuf from a mempool
	// This new mbuf contains one segment, which has a length of 0.
	// The pointer to data is initialized to have some bytes of headroom in the buffer (if buffer size allows).
	struct rte_mbuf *mb = rte_pktmbuf_alloc(foo_pool);

	if (mb == nullptr) {
		rte_exit(EXIT_FAILURE, "mbuf alloc failed: %s\n", rte_strerror(rte_errno));
	}

	mb->next = nullptr;
	mb->buf_len = 20;
	mb->data_len = 40;

	RTE_LOG(INFO, USER1, "mbuf allocated %p\n", mb);

	rte_pktmbuf_free(mb);
}

static inline void ethernet_device_lib(void)
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

	struct rte_eth_dev_info dev_info = { 0 };

	int ret = rte_eth_dev_info_get(0, &dev_info);

	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot get device info\n");

	printf("RX capabilities:\n");

#define PRINT_RX(flag)                         \
	if (dev_info.rx_offload_capa & (flag)) \
	printf("  %s\n", #flag)

	PRINT_RX(RTE_ETH_RX_OFFLOAD_VLAN_STRIP);
	PRINT_RX(RTE_ETH_RX_OFFLOAD_IPV4_CKSUM);
	PRINT_RX(RTE_ETH_RX_OFFLOAD_UDP_CKSUM);
	PRINT_RX(RTE_ETH_RX_OFFLOAD_TCP_CKSUM);
	PRINT_RX(RTE_ETH_RX_OFFLOAD_SCATTER);
	PRINT_RX(RTE_ETH_RX_OFFLOAD_RSS_HASH);

	printf("TX capabilities:\n");

#define PRINT_TX(flag)                         \
	if (dev_info.tx_offload_capa & (flag)) \
	printf("  %s\n", #flag)

	PRINT_TX(RTE_ETH_TX_OFFLOAD_VLAN_INSERT);
	PRINT_TX(RTE_ETH_TX_OFFLOAD_IPV4_CKSUM);
	PRINT_TX(RTE_ETH_TX_OFFLOAD_UDP_CKSUM);
	PRINT_TX(RTE_ETH_TX_OFFLOAD_TCP_CKSUM);
	PRINT_TX(RTE_ETH_TX_OFFLOAD_MULTI_SEGS);

	struct rte_eth_rxmode rxmode = { .mtu = 1500, .offloads = 0 };

	struct rte_eth_conf conf = {
		.link_speeds = RTE_ETH_LINK_SPEED_100M,
		.lpbk_mode = 0,
		.rxmode = rxmode,
		.txmode = { .offloads = 0 },
	};

	ret = rte_eth_dev_configure(0, 1, 1, &conf);

	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Cannot configure port: %s\n", rte_strerror(-ret));

	RTE_LOG(INFO, USER1, "dev_configure success\n");
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
     *      steps needed for sending Ethernet packets over write
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

// roadmap = rte_mempool → rte_mbuf → rte_ethdev (rx)
void learn()
{
	init_eal();

	rte_state_init();
	const struct lcore_state *state = RTE_LCORE_VAR_LCORE(0, lcore_states);
	printf("a = %d, b = %d\n", state->a, state->b);

	mem_pool_lib();
	mbuf_lib();

	ethernet_device_lib();

	// (todo): find out why i am getting seg fault when calling this
	// rte_eal_cleanup();
}
