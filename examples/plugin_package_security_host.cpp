#include "digitor/plugin_package_security.hpp"

// Host integration outline: verify publisher trust, create a private staging
// directory, extract validated entries there, fsync as appropriate, then
// atomically rename/swap the completed package into its versioned active path.
// DigitorEngine validates policy and coordinates rollback; the Digitor app owns
// trusted keys, revocation refresh and all free/paid entitlement decisions.
