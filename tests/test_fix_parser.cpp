/**
 * @file test_fix_parser.cpp
 * @brief FIX protocol parser unit tests
 */

#include <iostream>
#include "protocol/fix_message.hpp"

using namespace hft;

#define ASSERT(cond) \
    if (!(cond)) throw std::runtime_error("Assertion failed: " #cond)

void run_fix_parser_tests() {
    std::cout << "\n=== FIX Parser Tests ===\n";
    
    // Test 1: Basic message parsing
    {
        std::cout << "  Basic message parsing... ";
        
        std::string msg = "8=FIX.4.4\x01""9=100\x01""35=D\x01""49=SENDER\x01""56=TARGET\x01""10=000\x01";
        
        FixMessage fix_msg;
        ASSERT(fix_msg.parse(msg));
        
        ASSERT(fix_msg.msg_type() == "D");
        
        auto sender = fix_msg.get_field(fix_tag::SenderCompID);
        ASSERT(sender.has_value());
        ASSERT(*sender == "SENDER");
        
        auto target = fix_msg.get_field(fix_tag::TargetCompID);
        ASSERT(target.has_value());
        ASSERT(*target == "TARGET");
        
        std::cout << "PASSED\n";
    }
    
    // Test 2: Integer field parsing
    {
        std::cout << "  Integer field parsing... ";
        
        std::string msg = "8=FIX.4.4\x01""9=50\x01""35=8\x01""38=1000\x01""44=50000\x01""10=000\x01";
        
        FixMessage fix_msg;
        ASSERT(fix_msg.parse(msg));
        
        auto qty = fix_msg.get_int(fix_tag::OrderQty);
        ASSERT(qty.has_value());
        ASSERT(*qty == 1000);
        
        auto price = fix_msg.get_int(fix_tag::Price);
        ASSERT(price.has_value());
        ASSERT(*price == 50000);
        
        std::cout << "PASSED\n";
    }
    
    // Test 3: Double field parsing
    {
        std::cout << "  Double field parsing... ";
        
        std::string msg = "8=FIX.4.4\x01""9=50\x01""35=8\x01""44=123.456\x01""10=000\x01";
        
        FixMessage fix_msg;
        ASSERT(fix_msg.parse(msg));
        
        auto price = fix_msg.get_double(fix_tag::Price);
        ASSERT(price.has_value());
        ASSERT(*price > 123.45 && *price < 123.46);
        
        std::cout << "PASSED\n";
    }
    
    // Test 4: Message builder
    {
        std::cout << "  Message builder... ";
        
        FixMessageBuilder builder;
        builder.begin("D", "SENDER", "TARGET", 1)
               .add_field(fix_tag::ClOrdID, "ORDER123")
               .add_field(fix_tag::Symbol, "BTC-USD")
               .add_field(fix_tag::Side, '1')
               .add_field(fix_tag::OrderQty, static_cast<std::int64_t>(100))
               .add_field(fix_tag::Price, 50000.0);
        
        std::string msg = builder.build();
        
        // Parse it back
        FixMessage parsed;
        ASSERT(parsed.parse(msg));
        ASSERT(parsed.msg_type() == "D");
        
        auto cl_ord_id = parsed.get_field(fix_tag::ClOrdID);
        ASSERT(cl_ord_id.has_value());
        ASSERT(*cl_ord_id == "ORDER123");
        
        std::cout << "PASSED\n";
    }
    
    // Test 5: Side conversion
    {
        std::cout << "  Side conversion... ";
        
        ASSERT(side_to_fix(Side::BUY) == '1');
        ASSERT(side_to_fix(Side::SELL) == '2');
        
        ASSERT(fix_to_side('1').value() == Side::BUY);
        ASSERT(fix_to_side('2').value() == Side::SELL);
        ASSERT(!fix_to_side('X').has_value());
        
        std::cout << "PASSED\n";
    }
    
    // Test 6: Order type conversion
    {
        std::cout << "  Order type conversion... ";
        
        ASSERT(order_type_to_fix(OrderType::MARKET) == '1');
        ASSERT(order_type_to_fix(OrderType::LIMIT) == '2');
        ASSERT(order_type_to_fix(OrderType::STOP_LIMIT) == '4');
        
        std::cout << "PASSED\n";
    }
    
    // Test 7: Field existence check
    {
        std::cout << "  Field existence check... ";
        
        std::string msg = "8=FIX.4.4\x01""9=20\x01""35=D\x01""10=000\x01";
        
        FixMessage fix_msg;
        ASSERT(fix_msg.parse(msg));
        
        ASSERT(fix_msg.has_field(fix_tag::BeginString));
        ASSERT(fix_msg.has_field(fix_tag::MsgType));
        ASSERT(!fix_msg.has_field(fix_tag::OrderQty));
        ASSERT(!fix_msg.has_field(fix_tag::Price));
        
        std::cout << "PASSED\n";
    }
    
    // Test 8: Clear and reuse
    {
        std::cout << "  Clear and reuse... ";
        
        FixMessage fix_msg;
        
        std::string msg1 = "8=FIX.4.4\x01""9=20\x01""35=A\x01""10=000\x01";
        ASSERT(fix_msg.parse(msg1));
        ASSERT(fix_msg.msg_type() == "A");
        
        fix_msg.clear();
        
        std::string msg2 = "8=FIX.4.4\x01""9=20\x01""35=5\x01""10=000\x01";
        ASSERT(fix_msg.parse(msg2));
        ASSERT(fix_msg.msg_type() == "5");
        
        std::cout << "PASSED\n";
    }
    
    std::cout << "  All FIX parser tests passed!\n";
}

