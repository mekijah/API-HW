/**
 * simple decentralized exchange
 * smart contract on eos
 *
 */

#include "exchange.hpp"
#include <eosiolib/currency.hpp>

using namespace eosio;

void exchange::add_order(uint64_t scope, account_name maker, asset quantity, uint64_t price, account_name contract) {
    order_index orders(_self, scope);
    print("add order, maker: ", name{maker}, " quantity: ", quantity, " price: ", asset(price, settlement_token_symbol),
          "\n");
    if (quantity.amount > 0) {
        orders.emplace(maker, [&](auto &r) {
            r.id = orders.available_primary_key();
            r.price = price;
            r.quantity = quantity;
            r.maker = maker;
            r.contract = contract;
        });
    }
}

void exchange::cancelorder(account_name scope, uint64_t order_id) {
    order_index orders(_self, scope);

    const auto& order = orders.get( order_id, "no order object found" );
    withdraw(order.contract, order.maker, order.quantity);
    orders.erase( order );

}

void exchange::deposit(account_name contract, account_name user, asset quantity) {
    print(name{user}, " deposit ", quantity, "\n");
    if (quantity.amount == 0) {
        print("amoutn equals to 0 abord\n");
        return;
    }
    action(
            permission_level{user, N(active)},
            contract, N(transfer),
            std::make_tuple(user, _self, quantity, std::string("deposit"))
    ).send();
}

void exchange::transfer(account_name contract, account_name from, account_name to, asset quantity) {
    print(name{from}, " transfer ", quantity, " to ", name{to}, "\n");
    action(
            permission_level{from, N(active)},
            contract, N(transfer),
            std::make_tuple(from, to, quantity, std::string("transfer"))
    ).send();
}


void exchange::withdraw(account_name contract, account_name user, asset quantity) {
    print(name{user}, " withdraw ", quantity, "\n");
    if (quantity.amount <= 0) {
        print("amoutn equals to 0 abord\n");
        return;
    }
//    eosio_assert( quantity.amount > 0, "insufficient bid" );
    action act(
            permission_level{_self, N(active)},
            contract, N(transfer),
            std::make_tuple(_self, user, quantity, std::string("withdraw"))
    );
    act.send();
}

asset exchange::to_settlement_token(asset quantity, uint64_t price, bool floor) {
    eosio_assert(quantity.amount >=0 , "insufficient amount");
    //this is a simple logic, need to add more rules
    uint64_t amt = quantity.amount * price;

    int64_t p = (int64_t) quantity.symbol.precision();
    int64_t p10 = 1;

    while (p > 0) {
        p10 *= 10;
        --p;
    }
    // this arithmetician work for eos rule like eos.amount*10000
    uint64_t res = amt / p10;

    if (!floor && res * p10 != amt)
        res++;
    return asset(res, settlement_token_symbol);
}

asset exchange::asset_min(asset a, asset b) {
    if (a > b) return b;
    else return a;
}

//sell
void exchange::ask(account_name maker, asset quantity, uint64_t price, account_name ask_contract) {
    //Top up other tokens and sell
    //deposit(exchange_token_contract, maker, quantity);

    //Find all buyers
    order_index bid_orders(_self, N(bid));
    //Find all buyers whose purchase price is higher than price, sorted by price time
    auto bid_orders_by_price = bid_orders.get_index<N(byprice)>();
    auto bid_it = bid_orders_by_price.end();
    auto bid_end = bid_orders_by_price.lower_bound(price);

    auto left = quantity;
    asset maker_receive(0, settlement_token_symbol);

//    require_auth(maker);
//    require_recipient(maker);

    while (left.amount > 0 && bid_it != bid_end) {
        bid_it--;
        print("bid order id: ", bid_it->id, " price: ", bid_it->price, "\n");

        auto exchange_quantity = asset_min(bid_it->quantity, left);

        auto settlement_token_quantity = to_settlement_token(exchange_quantity, price, true);

        maker_receive += settlement_token_quantity;
        //withdraw(exchange_token_contract, bid_it->maker, exchange_quantity);
        transfer(ask_contract, maker, bid_it->maker, exchange_quantity);

        left -= exchange_quantity;

        if (bid_it->quantity == exchange_quantity) {
            bid_it = bid_orders_by_price.erase(bid_it);
        } else {
            bid_orders_by_price.modify(bid_it, maker, [&](auto &r) {
                r.quantity -= exchange_quantity;
            });
        }
    }
    deposit(ask_contract, maker, left);
    add_order(N(ask), maker, left, price, ask_contract);

    //just support xx/EOS trade now, maybe improve it to xx/xx
    withdraw(settlement_token_contract, maker, maker_receive);
}

//buy
void exchange::bid(account_name maker, asset quantity, uint64_t price, asset bid_currency, account_name bid_contract) {

    //Find sell order
    order_index ask_orders(_self, N(ask));
    //Find all sellers selling below price sorted by price time
    auto ask_orders_by_price = ask_orders.get_index<N(byprice)>();
    auto ask_it = ask_orders_by_price.begin();
    auto ask_end = ask_orders_by_price.upper_bound(price);

    auto left = quantity;
    asset maker_receive(0, bid_currency.symbol);

//    require_auth(maker);
//    require_recipient(maker);

    while (left.amount > 0 && ask_it != ask_end) {
        print("ask order id: ", ask_it->id, " price: ", ask_it->price, "\n");

        auto exchange_quantity = asset_min(ask_it->quantity, left);

        auto settlement_token_quantity = to_settlement_token(exchange_quantity, ask_it->price, false);

        maker_receive += exchange_quantity;
        transfer(settlement_token_contract, maker, ask_it->maker, settlement_token_quantity);

        left -= exchange_quantity;

        if (ask_it->quantity == exchange_quantity) {
            ask_orders_by_price.erase(ask_it);

        }
        else {
            ask_orders_by_price.modify(ask_it, maker, [&](auto &r) {
                r.quantity -= exchange_quantity;
            });
        }
        ask_it++;
    }


    deposit(settlement_token_contract, maker, to_settlement_token(left, price, false));
    add_order(N(bid), maker, left, price, settlement_token_contract);

    withdraw(bid_contract, maker, maker_receive);
}

EOSIO_ABI(exchange, (bid)(ask)(cancelorder))