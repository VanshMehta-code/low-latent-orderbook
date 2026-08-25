

#ifndef ORDER_BOOK_H
#define ORDER_BOOK_H

#include <cstdint>

enum class order_range {
  penny, ///< For penny stocks (small price range)
  mid,   ///< For mid-priced stocks (typical use)
  wide   ///< For high-priced stocks or crypto
};

struct order {
  uint32_t quantity;  ///< Remaining shares (0 = lazy-deleted)
  uint32_t price_idx; ///< Index into price_list array
  uint32_t next;      ///< Next order in FIFO (-1 = none)
  uint32_t prev;      ///< Previous order in FIFO (-1 = none)
  bool is_buy;        ///< true = buy, false = sell
};

struct price_level {
  uint32_t buy_head = -1;  ///< First buy order index (-1 = empty)
  uint32_t buy_tail = -1;  ///< Last buy order index (-1 = empty)
  uint32_t sell_head = -1; ///< First sell order index (-1 = empty)
  uint32_t sell_tail = -1; ///< Last sell order index (-1 = empty)
};

class order_book {
private:
  static constexpr double TICK = 0.01; ///< Minimum price increment ($0.01)
  static constexpr uint32_t SIZE = 1'000'000; ///< Max concurrent orders

  // Core data structures
  order *order_list;       ///< [SIZE] All orders indexed by order_id
  price_level *price_list; ///< [range] FIFO queues indexed by price
  uint32_t *buy_quantity;  ///< [range] Sum of buy qty at each price
  uint32_t *sell_quantity; ///< [range] Sum of sell qty at each price

  // Cached best prices (lazy evaluation)
  double low_ask;  ///< Cached best ask price
  double high_bid; ///< Cached best bid price

  // Configuration
  uint32_t starting_order_id; ///< First valid order ID
  uint32_t range;             ///< Number of price levels
  double base_price;          ///< Lowest price in range

  uint32_t execute_buy(uint32_t quantity, uint32_t price_idx);

  uint32_t execute_sell(uint32_t quantity, uint32_t price_idx);

  // Helper functions
  int32_t order_id_to_idx(uint32_t order_id);
  int32_t price_to_idx(double price);

public:
  order_book(order_range order_range, double current_price,
             uint32_t starting_order_id);

  ~order_book();

  void add_order(uint32_t order_id, double price, uint32_t quantity,
                 bool is_buy);

  void cancel_order(uint32_t order_id);

  void execute_order(uint32_t order_id, uint32_t quantity, bool is_buy);

  double get_lowest_ask();

  double get_highest_bid();
};

#endif // ORDER_BOOK_H
