#pragma once

#include <stdint.h>

typedef enum : uint64_t {
	TX_OFFLOAD_VLAN_INSERT = 1ULL << 0,
	TX_OFFLOAD_IPV4_CKSUM = 1ULL << 1,
	TX_OFFLOAD_UDP_CKSUM = 1ULL << 2,
	TX_OFFLOAD_TCP_CKSUM = 1ULL << 3,
	TX_OFFLOAD_TCP_TSO = 1ULL << 5,
	TX_OFFLOAD_UDP_TSO = 1ULL << 6,
} tx_offload_flags;

typedef enum : uint64_t {
	RX_OFFLOAD_VLAN_STRIP = 1ULL << 0,
	RX_OFFLOAD_IPV4_CKSUM = 1ULL << 1,
	RX_OFFLOAD_UDP_CKSUM = 1ULL << 2,
	RX_OFFLOAD_TCP_CKSUM = 1ULL << 3,
	RX_OFFLOAD_SCATTER = 1ULL << 13,
} rx_offload_flags;

struct dpdk_port_config {
	uint16_t port_id;
	uint16_t nb_rx_queues;
	uint16_t nb_tx_queues;
	uint16_t nb_rx_descriptors;
	uint16_t nb_tx_descriptors;
	tx_offload_flags tx_flags;
	rx_offload_flags rx_flags;
	struct rte_mempool *_Nonnull rx_mempool; /* (todo): investigate can we have better design */
};

[[nodiscard("eal initialization may fail"), gnu::cold]] int init_eal(int argc, char **argv);
[[nodiscard("configuration may fail"), gnu::cold]] int configure_port(struct dpdk_port_config *_Nonnull config);
