#include "cppkit/log/log.hpp"
#include "cppkit/time.hpp"

void logger()
{
    auto& logger = cppkit::log::Logger::instance();
    logger.setRotationSize(1024 * 1024 * 10);
    logger.setRotation(cppkit::log::Rotation::Daily);
    logger.setMaxFiles(5);
    logger.setFileNamePattern("{year}-{month}/access-{date}.log");

    CK_LOG_INFO("program started...");

    CK_LOG_ERROR("level = {}", cppkit::Time::Now().Format());
}

int main()
{
    logger();
    // CK_LOG_SET_LEVEL(cppkit::log::Level::Info);
    // CK_LOG_SET_STDOUT(true);
    // CK_LOG_SET_ROTATION(cppkit::log::Rotation::Daily);
    // CK_LOG_SET_FILENAME_PATTERN("{year}-{month}/access-{date}.log");
    //
    // CK_LOG_INFO("program started...");
    //
    // CK_LOG_INFO("level = {}", cppkit::Time::Now().Format());

    // CK_LOG_FLUSH();

    std::this_thread::sleep_for(std::chrono::seconds(3));
    return 0;
}
