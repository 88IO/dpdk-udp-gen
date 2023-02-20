## DPDK UDP PACKET GENERATOR

- UDPパケットを生成して送信するDPDKアプリケーション

### Txチェックサムオフロード

1. ポートのオフロード設定を記述

```c
struct rte_eth_conf port_conf = {
    .txmode = {
        .offloads =
            RTE_ETH_TX_OFFLOAD_IPV4_CKSUM  |
            RTE_ETH_TX_OFFLOAD_UDP_CKSUM 
    }
};
```

2. デバイス情報を取得

```c
struct rte_eth_dev_info dev_info;
retval = rte_eth_dev_info_get(port, &dev_info);
```

3. デバイスで利用可能な機能をマスク

```c
port_conf.txmode.offloads &= dev_info.tx_offload_capa;
```

4. デバイスを初期化

```c
retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
```

5. キューを初期化

Rxキュー
```c
struct rte_eth_rxconf rxconf;
rxconf = dev_info.default_rxconf;
rxconf.offloads = port_conf.rxmode.offloads;
retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
		rte_eth_dev_socket_id(port), &rxconf, mbuf_pool);
```

Txキュー
```c
struct rte_eth_txconf txconf;
txconf = dev_info.default_txconf;
txconf.offloads = port_conf.txmode.offloads;
retval = rte_eth_tx_queue_setup(port, q, nb_txd,
		rte_eth_dev_socket_id(port), &txconf);
```

6. Txチェックサムオフロードを適用するmbufにメタ情報を記述

```c
struct rte_mbuf *m;
m->l2_len = sizeof(struct rte_ether_hdr);
m->l3_len = sizeof(struct rte_ipv4_hdr);
```

IPV4(L3)Txチェックサムオフロード
```c
m->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_IP_CKSUM);
```

UDP(L4)Txチェックサムオフロード
```c
m->ol_flags |= (RTE_MBUF_F_TX_IPV4 | RTE_MBUF_F_TX_UDP_CKSUM);

// 疑似ヘッダの計算が必要
uint32_t pseudo_cksum = csum32_add(
	csum32_add(iph->src_addr, iph->dst_addr),
	(iph->next_proto_id << 24) + udph->dgram_len
);
udph->dgram_cksum = csum16_add(pseudo_cksum & 0xFFFF, pseudo_cksum >> 16);
```

### 実行例

```
make
./build/udp_gen
```

### 受信側動作確認

```
tcpdump -vvv
```

例
```
09:13:14.173826 IP (tos 0x0, ttl 64, id 0, offset 0, flags [none], proto UDP (17), length 114)
    192.168.0.31.1000 > 192.168.0.32.2000: [udp sum ok] UDP, length 86
```

### 参考

https://doc.dpdk.org/guides/nics/features.html