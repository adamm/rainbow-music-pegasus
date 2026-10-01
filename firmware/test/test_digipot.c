// Tests for the MCP41050 digital potentiometer that sets the mic's gain.

#include <stdint.h>

#include "config.h"
#include "digipot.h"
#include "fake_idf.h"
#include "unity.h"

void setUp(void)
{
    fake_idf_reset();
    digipot_init();
}

void tearDown(void)
{
}


void test_init_uses_an_spi_mode_and_clock_the_mcp41050_supports(void)
{
    TEST_ASSERT_TRUE_MESSAGE(fake.spi_device.mode == 0 || fake.spi_device.mode == 3,
                             "The MCP41050 only supports SPI modes 0 and 3");
    TEST_ASSERT_LESS_OR_EQUAL_MESSAGE(10000000, fake.spi_device.clock_speed_hz,
                                      "The MCP41050's SPI clock is at most 10 MHz");

    TEST_ASSERT_EQUAL(CONFIG_GPIO_DIGIPOT_CS, fake.spi_device.spics_io_num);
    TEST_ASSERT_EQUAL(CONFIG_GPIO_DIGIPOT_MOSI, fake.spi_bus.mosi_io_num);
    TEST_ASSERT_EQUAL(CONFIG_GPIO_DIGIPOT_CLK, fake.spi_bus.sclk_io_num);
}


void test_set_value_sends_a_write_command_then_the_wiper_code(void)
{
    digipot_set_value(200);

    TEST_ASSERT_EQUAL(1, fake.spi_transactions);
    TEST_ASSERT_EQUAL(16, fake.spi_sent.length);
    TEST_ASSERT_BITS_HIGH(SPI_TRANS_USE_TXDATA, fake.spi_sent.flags);
    TEST_ASSERT_EQUAL_HEX8(0x11, fake.spi_sent.tx_data[0]);  // write data, pot 0
    TEST_ASSERT_EQUAL_HEX8(200, fake.spi_sent.tx_data[1]);
}


void test_set_value_clamps_to_the_wiper_range(void)
{
    digipot_set_value(-1);
    TEST_ASSERT_EQUAL_HEX8(0, fake.spi_sent.tx_data[1]);

    digipot_set_value(256);
    TEST_ASSERT_EQUAL_HEX8(255, fake.spi_sent.tx_data[1]);
}


int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_init_uses_an_spi_mode_and_clock_the_mcp41050_supports);
    RUN_TEST(test_set_value_sends_a_write_command_then_the_wiper_code);
    RUN_TEST(test_set_value_clamps_to_the_wiper_range);
    return UNITY_END();
}
