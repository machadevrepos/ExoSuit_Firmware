#include <cstdint>
#include <iostream>

#include <exo/storage/node_runtime_config.h>

namespace {

int failures = 0;

#define EXPECT_TRUE(expr) do { \
    if (!(expr)) { \
        std::cerr << __FILE__ << ':' << __LINE__ << ": expected " #expr "\n"; \
        ++failures; \
    } \
} while (0)

}  // namespace

int main()
{
    static_assert(exo::node_runtime_config::kNodeIdMin == 1U,
                  "Normal node IDs must start at one");
    static_assert(exo::node_runtime_config::kNodeIdMax == 12U,
                  "Normal node IDs must extend through Node12");

    EXPECT_TRUE(!exo::node_runtime_config::is_valid_node_id(0U));
    EXPECT_TRUE(exo::node_runtime_config::is_valid_node_id(1U));
    EXPECT_TRUE(exo::node_runtime_config::is_valid_node_id(6U));
    EXPECT_TRUE(exo::node_runtime_config::is_valid_node_id(7U));
    EXPECT_TRUE(exo::node_runtime_config::is_valid_node_id(12U));
    EXPECT_TRUE(!exo::node_runtime_config::is_valid_node_id(13U));

    if (failures != 0) {
        std::cerr << failures << " node runtime configuration check(s) failed\n";
        return 1;
    }
    std::cout << "node runtime configuration checks passed\n";
    return 0;
}
