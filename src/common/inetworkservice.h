#pragma once

// Seam for the deferred update-check/feedback-submission features
// (constitution Principle 5: local logic stays decoupled from anything that
// will eventually talk to a server, without building the server now).
// Nothing calls into a real implementation yet - only NullNetworkService
// exists, and the app makes zero outbound network calls in this phase
// (FR-024).
class INetworkService {
public:
    virtual ~INetworkService() = default;

    // No members yet - this is deliberately just a marker seam until a
    // future feature (update check, feedback submission) needs one.
};

class NullNetworkService : public INetworkService {
};
