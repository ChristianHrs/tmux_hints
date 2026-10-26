cmake_minimum_required(VERSION 3.16)
project(krunner_tmux VERSION 1.0 LANGUAGES CXX)

set(QT_MIN_VERSION "6.5.0")
set(KF_MIN_VERSION "6.0.0")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(ECM ${KF_MIN_VERSION} REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH ${ECM_MODULE_PATH})

include(KDEInstallDirs)
include(KDECMakeSettings)
include(FeatureSummary)

find_package(Qt6 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui)
find_package(KF6 ${KF_MIN_VERSION} REQUIRED COMPONENTS Runner KIO CoreAddons)

kcoreaddons_add_plugin(krunner_tmux
    SOURCES tmuxrunner.cpp tmuxrunner.h tmuxcommands.h tmuxbindings.h
    INSTALL_NAMESPACE "kf6/krunner")

target_link_libraries(krunner_tmux
    Qt6::Core
    Qt6::Gui
    KF6::Runner
    KF6::KIOGui
    KF6::CoreAddons)

feature_summary(WHAT ALL INCLUDE_QUIET_PACKAGES FATAL_ON_MISSING_REQUIRED_PACKAGES)
