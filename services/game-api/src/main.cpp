#include <drogon/drogon.h>

#include <functional>

int main() {
  drogon::app().registerHandler(
      "/hello",
      [](const drogon::HttpRequestPtr&,
         std::function<void(const drogon::HttpResponsePtr&)>&& reply) {
        Json::Value body;
        body["message"] = "hello-world";
        reply(drogon::HttpResponse::newHttpJsonResponse(body));
      },
      {drogon::Get});

  drogon::app()
      .setFileTypes({})
      .setImplicitPageEnable(false)
      .setUploadPath("/tmp/mini-checkers-uploads")
      .addListener("0.0.0.0", 8000)
      .run();
}
