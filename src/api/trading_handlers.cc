#include "src/api/trading_handlers.h"

#include <cstdint>
#include <string>
#include <utility>

#include "absl/status/status.h"
#include "absl/status/statusor.h"
#include "absl/strings/str_cat.h"
#include "nlohmann/json.hpp"
#include "src/auth/session_auth.h"
#include "src/auth/session_store.h"
#include "src/common/money.h"
#include "src/common/status_macros.h"
#include "src/common/symbol.h"
#include "src/marketdata/provider.h"
#include "src/trading/market_calendar.h"
#include "src/trading/order_math.h"

namespace firefly {
namespace {

struct OrderRequest {
  Symbol symbol;
  OrderSide side = OrderSide::kBuy;
  int64_t quantity = 0;
};

absl::StatusOr<OrderRequest> ParseOrderRequest(const std::string& body_json) {
  const nlohmann::json body = nlohmann::json::parse(
      body_json, /*cb=*/nullptr, /*allow_exceptions=*/false);
  if (body.is_discarded() || !body.is_object()) {
    return absl::InvalidArgumentError("request body must be a JSON object");
  }
  if (!body.contains("symbol") || !body["symbol"].is_string()) {
    return absl::InvalidArgumentError("missing string field: symbol");
  }
  ASSIGN_OR_RETURN(Symbol symbol,
                   Symbol::Parse(body["symbol"].get<std::string>()));
  if (!body.contains("side") || !body["side"].is_string()) {
    return absl::InvalidArgumentError("missing string field: side");
  }
  ASSIGN_OR_RETURN(const OrderSide side,
                   ParseOrderSide(body["side"].get<std::string>()));
  // Strictly a JSON integer: 2.0, "2", and 1e6 are all rejected.
  if (!body.contains("quantity") || !body["quantity"].is_number_integer()) {
    return absl::InvalidArgumentError("quantity must be a whole number");
  }
  const int64_t quantity = body["quantity"].get<int64_t>();
  if (quantity < 1 || quantity > kMaxOrderQuantity) {
    return absl::InvalidArgumentError(absl::StrCat(
        "quantity must be a whole number between 1 and ", kMaxOrderQuantity));
  }
  return OrderRequest{
      .symbol = std::move(symbol), .side = side, .quantity = quantity};
}

}  // namespace

absl::StatusOr<nlohmann::json> PlaceOrderJson(const TradingDeps& deps,
                                              const std::string& cookie_token,
                                              const std::string& body_json) {
  ASSIGN_OR_RETURN(const SessionRecord session,
                   RequireSession(deps.session_auth, cookie_token));
  ASSIGN_OR_RETURN(const OrderRequest request, ParseOrderRequest(body_json));
  ASSIGN_OR_RETURN(const bool exists, deps.instruments.Exists(request.symbol));
  if (!exists) {
    return absl::NotFoundError(
        absl::StrCat("unknown symbol: ", request.symbol.str()));
  }
  if (!deps.allow_closed_market && !IsMarketOpen(deps.clock.Now())) {
    return absl::FailedPreconditionError("market is closed");
  }
  if (deps.provider == nullptr) {
    return absl::UnavailableError("market data not configured");
  }
  // The execution quote, fetched before the store's transaction begins.
  ASSIGN_OR_RETURN(const Trade trade,
                   deps.provider->GetLatestTrade(request.symbol));
  ASSIGN_OR_RETURN(
      const OrderResult result,
      deps.trading.ExecuteOrder(session.user_id, request.symbol, request.side,
                                request.quantity, trade.price_e4));
  nlohmann::json body = {{"order_id", result.order_id},
                         {"symbol", request.symbol.str()},
                         {"side", std::string(OrderSideName(request.side))},
                         {"quantity", request.quantity},
                         {"price", PriceE4ToString(result.price_e4)},
                         {"cash_delta_cents", result.cash_delta_cents},
                         {"cash_cents", result.cash_cents}};
  body["position"] =
      result.position_quantity == 0
          ? nlohmann::json(nullptr)
          : nlohmann::json{
                {"quantity", result.position_quantity},
                {"avg_price", PriceE4ToString(result.position_avg_price_e4)}};
  return body;
}

absl::StatusOr<nlohmann::json> GetPortfolioJson(
    const TradingDeps& deps, const std::string& cookie_token) {
  ASSIGN_OR_RETURN(const SessionRecord session,
                   RequireSession(deps.session_auth, cookie_token));
  ASSIGN_OR_RETURN(const Portfolio portfolio,
                   deps.trading.GetPortfolio(session.user_id));
  nlohmann::json positions = nlohmann::json::array();
  for (const PositionRecord& position : portfolio.positions) {
    positions.push_back(
        {{"symbol", position.symbol.str()},
         {"quantity", position.quantity},
         {"avg_price", PriceE4ToString(position.avg_price_e4)}});
  }
  return nlohmann::json{{"cash_cents", portfolio.cash_cents},
                        {"positions", std::move(positions)}};
}

}  // namespace firefly
