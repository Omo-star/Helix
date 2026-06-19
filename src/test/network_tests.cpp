#include "test/fixture.h"

#include "network/fetcher.h"

TestResult RunNetworkTests() {
    TestResult result;

    {
        auto res = FetchUrl("data:text/plain,Hello%20World%21");
        std::string actual = (res.success ? "success " : "failure ")
            + res.contentType + " " + res.body + "\n";
        ExpectEqual("network/data-url/plain", actual,
            "success text/plain Hello World!\n", result);
    }

    {
        auto res = FetchUrl("data:image/png;base64,QUJDRA%3D%3D");
        std::string actual = (res.success ? "success " : "failure ")
            + res.contentType + " "
            + std::to_string(res.body.size()) + " " + res.body + "\n";
        ExpectEqual("network/data-url/base64", actual,
            "success image/png 4 ABCD\n", result);
    }

    return result;
}
