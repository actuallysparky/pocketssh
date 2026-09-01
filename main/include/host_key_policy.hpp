#pragma once

#include <sstream>
#include <string>

namespace pocketssh {

enum class HostKeyTrustState {
    Matching,
    Unknown,
    Changed,
};

enum class HostKeyPolicyAction {
    Allow,
    Prompt,
    Reject,
};

// The on-disk format is deliberately simple and portable across the device's
// FAT volume: host, port, key type, then hex-encoded key material.
inline HostKeyTrustState classify_known_host(const std::string &known_hosts,
                                             const std::string &host,
                                             int port,
                                             const std::string &key_type,
                                             const std::string &key_material)
{
    std::istringstream lines(known_hosts);
    std::string line;
    while (std::getline(lines, line)) {
        std::istringstream fields(line);
        std::string known_host;
        std::string known_port;
        std::string known_type;
        std::string known_key;
        if (!(fields >> known_host >> known_port >> known_type >> known_key)) continue;
        if (known_host == host && known_port == std::to_string(port)) {
            return (known_type == key_type && known_key == key_material)
                ? HostKeyTrustState::Matching : HostKeyTrustState::Changed;
        }
    }
    return HostKeyTrustState::Unknown;
}

inline HostKeyPolicyAction host_key_policy_action(HostKeyTrustState trust,
                                                   const std::string &normalized_policy)
{
    if (trust == HostKeyTrustState::Matching) return HostKeyPolicyAction::Allow;
    if (trust == HostKeyTrustState::Changed) return HostKeyPolicyAction::Reject;
    if (normalized_policy == "yes") return HostKeyPolicyAction::Reject;
    if (normalized_policy == "no") return HostKeyPolicyAction::Allow;
    return HostKeyPolicyAction::Prompt;
}

}  // namespace pocketssh
