#include "../include/order_book.h"
#include <cstdint>
#include <cstdio>
#include <sys/types.h>
#include <xmmintrin.h>

order_book::order_book(order_range order_range, double current_price,
                       uint32_t starting_order_id)
    : starting_order_id(starting_order_id),
      range((order_range == order_range::penny) ? 10'000
            : (order_range == order_range::mid) ? 100'000
                                                : 1'000'000),
      base_price(current_price - ((range * TICK) / 2)),
      low_ask(int(base_price) + (range * TICK) + 1), high_bid(-1) {
  order_list = new order[SIZE]();
  price_list = new price_level[range];
  buy_quantity = new uint32_t[range]();
  sell_quantity = new uint32_t[range]();
  fprintf(stderr, "---------- Project Activated ----------\n");
}

order_book::~order_book() {
  delete[] sell_quantity;
  delete[] buy_quantity;
  delete[] order_list;
  delete[] price_list;
  fprintf(stderr, "---------- Project Deactivated ----------\n");
}

// Safe index conversion
int32_t order_book::order_id_to_idx(uint32_t order_id) {
  if (order_id < starting_order_id)
    return -1;
  uint32_t idx = order_id - starting_order_id;
  return (idx >= SIZE) ? -1 : idx;
}

// Safe price conversion
int32_t order_book::price_to_idx(double price) {
  double offset = (price - base_price) / TICK;
  if (offset < 0 || offset >= range)
    return -1;
  return (int32_t)offset;
}

uint32_t order_book::execute_buy(uint32_t quantity, uint32_t price_idx) {
  if (sell_quantity[price_idx] == 0) {
    return quantity;
  }

  uint32_t curr = price_list[price_idx].sell_head;
  while (curr != -1 && quantity != 0 && sell_quantity[price_idx] != 0) {
    if (order_list[curr].next != -1) {
      _mm_prefetch((const char *)&order_list[order_list[curr].next],
                   _MM_HINT_T0);
    }

    if (order_list[curr].quantity == 0) {
      curr = order_list[curr].next;
      continue;
    }

    if (quantity < order_list[curr].quantity) {
      order_list[curr].quantity -= quantity;
      sell_quantity[price_idx] -= quantity;
      return 0;
    }

    uint32_t matched_qty = order_list[curr].quantity;
    quantity -= matched_qty;
    order_list[curr].quantity = 0;
    sell_quantity[price_idx] -= matched_qty;

    price_list[price_idx].sell_head = order_list[curr].next;
    curr = order_list[curr].next;
  }

  if (price_list[price_idx].sell_head == -1) {
    price_list[price_idx].sell_tail = -1;
  }

  // NEW: Update low_ask if we cleared the best ask level
  if (sell_quantity[price_idx] == 0 && price_list[price_idx].sell_head != -1) {
    price_list[price_idx].sell_head = -1;
    price_list[price_idx].sell_tail = -1;
  }

  return quantity;
}

uint32_t order_book::execute_sell(uint32_t quantity, uint32_t price_idx) {
  if (buy_quantity[price_idx] == 0) {
    return quantity;
  }

  uint32_t curr = price_list[price_idx].buy_head;
  while (curr != -1 && quantity != 0 && buy_quantity[price_idx] != 0) {
    if (order_list[curr].next != -1) {
      _mm_prefetch((const char *)&order_list[order_list[curr].next],
                   _MM_HINT_T0);
    }

    if (order_list[curr].quantity == 0) {
      curr = order_list[curr].next;
      continue;
    }

    if (quantity < order_list[curr].quantity) {
      order_list[curr].quantity -= quantity;
      buy_quantity[price_idx] -= quantity;
      return 0;
    }

    uint32_t matched_qty = order_list[curr].quantity;
    quantity -= matched_qty;
    order_list[curr].quantity = 0;
    buy_quantity[price_idx] -= matched_qty;

    price_list[price_idx].buy_head = order_list[curr].next;
    curr = order_list[curr].next;
  }

  // NEW: Update high_bid if we cleared the best bid level
  if (buy_quantity[price_idx] == 0 && price_list[price_idx].buy_head != -1) {
    price_list[price_idx].buy_head = -1;
    price_list[price_idx].buy_tail = -1;
  }

  return quantity;
}

void order_book::add_order(uint32_t order_id, double price, uint32_t quantity,
                           bool is_buy) {
  uint32_t price_idx = price_to_idx(price);
  if (price_idx < 0) {
    return;
  }
  _mm_prefetch((const char *)&price_list[price_idx], _MM_HINT_T0);

  int32_t order_idx = order_id_to_idx(order_id);
  if (order_idx < 0) {
    return;
  }
  _mm_prefetch((const char *)&order_list[order_idx], _MM_HINT_T0);

  price_level &temp_price_level = price_list[price_idx];
  if (is_buy && temp_price_level.sell_head != -1) {
    _mm_prefetch((const char *)&order_list[temp_price_level.sell_head],
                 _MM_HINT_T0);
  }
  if (!is_buy && temp_price_level.buy_head != -1) {
    _mm_prefetch((const char *)&order_list[temp_price_level.buy_head],
                 _MM_HINT_T0);
  }

  if (is_buy) {
    quantity = execute_buy(quantity, price_idx);

  } else {
    quantity = execute_sell(quantity, price_idx);
  }

  if (quantity == 0) {
    return;
  }

  if (is_buy) {
    if (price > high_bid) {
      high_bid = price;
    }
  } else {
    if (price < low_ask) {
      low_ask = price;
    }
  }

  if (is_buy) {
    buy_quantity[price_idx] += quantity;
  } else {
    sell_quantity[price_idx] += quantity;
  }
  uint32_t tail = (temp_price_level.buy_tail * (is_buy)) +
                  (temp_price_level.sell_tail * (!is_buy));
  if (tail != -1) {
    _mm_prefetch((const char *)&order_list[tail], _MM_HINT_T0);
  }

  bool is_first_buy = (temp_price_level.buy_head == (uint32_t)-1) && is_buy;
  temp_price_level.buy_head =
      (order_idx * is_first_buy) + (temp_price_level.buy_head * !is_first_buy);

  bool is_first_sell = (temp_price_level.sell_head == (uint32_t)-1) && !is_buy;
  temp_price_level.sell_head = (order_idx * is_first_sell) +
                               (temp_price_level.sell_head * !is_first_sell);

  if (is_buy) {
    temp_price_level.buy_tail = order_idx;
  } else {
    temp_price_level.sell_tail = order_idx;
  }

  order_list[order_idx] = {
      .quantity = quantity,
      .price_idx = price_idx,
      .next = (uint32_t)-1,
      .prev = tail,
      .is_buy = is_buy,
  };
  if (tail != -1) {
    order_list[tail].next = order_idx;
  }
}

void order_book::cancel_order(uint32_t order_id) {
  int32_t idx = order_id_to_idx(order_id); // Keep as int32_t
  if (idx < 0)
    return; // Invalid order_id
  _mm_prefetch((const char *)&price_list[order_list[idx].price_idx],
               _MM_HINT_T0);
  order &o = order_list[idx];
  if (o.quantity == 0)
    return;
  uint32_t price_idx = o.price_idx;
  _mm_prefetch((const char *)&buy_quantity[price_idx], _MM_HINT_T0);
  _mm_prefetch((const char *)&sell_quantity[price_idx], _MM_HINT_T0);
  _mm_prefetch((const char *)&price_list[price_idx], _MM_HINT_T0);

  price_level &pl = price_list[price_idx];
  bool is_buy = o.is_buy;

  if (is_buy) {
    if (o.prev != -1)
      order_list[o.prev].next = o.next;
    else
      pl.buy_head = o.next;
    if (o.next != -1)
      order_list[o.next].prev = o.prev;
    else
      pl.buy_tail = o.prev;
  } else {
    if (o.prev != -1)
      order_list[o.prev].next = o.next;
    else
      pl.sell_head = o.next;
    if (o.next != -1)
      order_list[o.next].prev = o.prev;
    else
      pl.sell_tail = o.prev;
  }

  if (is_buy) {
    buy_quantity[price_idx] -= o.quantity;
  } else {
    sell_quantity[price_idx] -= o.quantity;
  }
  o.quantity = 0;
}

void order_book::execute_order(uint32_t order_id, uint32_t quantity,
                               bool is_buy) {
  add_order(order_id, is_buy ? get_lowest_ask() : get_highest_bid(), quantity,
            is_buy);
}

double order_book::get_lowest_ask() {
  // Check if current low_ask is still valid
  int32_t current_idx = price_to_idx(low_ask);
  if (current_idx >= 0 && sell_quantity[current_idx] > 0) {
    return low_ask; // Still valid, return immediately
  }

  // Need to find new low_ask
  bool found = false;
  for (int32_t i = current_idx + 1; i < (int32_t)range; i++) {
    if (sell_quantity[i] > 0) {
      low_ask = (i * TICK) + base_price;
      found = true;
      break;
    }
  }

  if (!found) {
    low_ask = base_price + (range * TICK); // No asks exist
  }

  return low_ask;
}

double order_book::get_highest_bid() {
  // Check if current high_bid is still valid
  int32_t current_idx = price_to_idx(high_bid);
  if (current_idx >= 0 && buy_quantity[current_idx] > 0) {
    return high_bid; // Still valid, return immediately
  }

  // Need to find new high_bid
  bool found = false;
  for (int32_t i = current_idx - 1; i >= 0; i--) {
    if (buy_quantity[i] > 0) {
      high_bid = (i * TICK) + base_price;
      found = true;
      break;
    }
  }

  if (!found) {
    high_bid = -1; // No bids exist
  }

  return high_bid;
}
