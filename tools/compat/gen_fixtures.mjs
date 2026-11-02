// Generate cross-SDK test fixtures: build a matrix of transactions with
// stellar-base, emit {name, envelopeXdr(base64), txHash(hex)} per case.
// Ed25519 is deterministic, so the signed envelopes are byte-identical
// across SDKs built the same way.
import * as StellarSdk from "stellar-base";
import { writeFileSync } from "node:fs";

const net = StellarSdk.Networks.TESTNET;
const passphrase = net;

// deterministic keypair from a fixed seed
const kp = StellarSdk.Keypair.fromRawEd25519Seed(Buffer.alloc(32, 0x5a));
const seed = kp.secret();
const pub = kp.publicKey();
const usd = new StellarSdk.Asset("USD", pub);
const eur = new StellarSdk.Asset("EUR", pub);

function builder(opts = {}) {
  const acc = new StellarSdk.Account(pub, "9999999999");
  const b = new StellarSdk.TransactionBuilder(acc, {
    fee: "100",
    networkPassphrase: passphrase,
    timebounds: { minTime: 1000000, maxTime: 2000000 },
  });
  return b;
}

const cases = [];
function add(name, b) {
  const tx = b.build();
  tx.sign(kp);
  const env = tx.toEnvelope();
  cases.push({
    name,
    envelopeXdr: env.toXDR("base64"),
    txHash: tx.hash().toString("hex"),
  });
}

add("payment_native", builder()
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "12.3456789" })));
add("payment_usd", builder()
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: usd, amount: "0.0000001" })));
add("create_account", builder()
  .addOperation(StellarSdk.Operation.createAccount({ destination: pub, startingBalance: "1.5" })));
add("path_payment_strict_receive", builder()
  .addOperation(StellarSdk.Operation.pathPaymentStrictReceive({ sendAsset: usd, sendMax: "10", destination: pub, destAsset: StellarSdk.Asset.native(), destAmount: "5", path: [eur] })));
add("path_payment_strict_send", builder()
  .addOperation(StellarSdk.Operation.pathPaymentStrictSend({ sendAsset: StellarSdk.Asset.native(), sendAmount: "10", destination: pub, destAsset: usd, destMin: "5", path: [eur, usd] })));
add("manage_sell_offer", builder()
  .addOperation(StellarSdk.Operation.manageSellOffer({ selling: usd, buying: StellarSdk.Asset.native(), amount: "7.5", price: "2.5" })));
add("manage_sell_offer_delete", builder()
  .addOperation(StellarSdk.Operation.manageSellOffer({ selling: usd, buying: StellarSdk.Asset.native(), amount: "0", price: "2.5", offerId: "42" })));
add("manage_buy_offer", builder()
  .addOperation(StellarSdk.Operation.manageBuyOffer({ selling: StellarSdk.Asset.native(), buying: usd, buyAmount: "7.5", price: "0.4" })));
add("create_passive_sell_offer", builder()
  .addOperation(StellarSdk.Operation.createPassiveSellOffer({ selling: usd, buying: StellarSdk.Asset.native(), amount: "7.5", price: "0.4" })));
add("set_options_all", builder()
  .addOperation(StellarSdk.Operation.setOptions({
    inflationDest: pub, clearFlags: 1, setFlags: 3, masterWeight: 5,
    lowThreshold: 1, medThreshold: 2, highThreshold: 3, homeDomain: "example.com",
    signer: { ed25519PublicKey: pub, weight: 7 },
  })));
add("set_options_min", builder()
  .addOperation(StellarSdk.Operation.setOptions({ masterWeight: 0 })));
add("change_trust", builder()
  .addOperation(StellarSdk.Operation.changeTrust({ asset: usd, limit: "1000" })));
add("change_trust_pool", builder()
  .addOperation(StellarSdk.Operation.changeTrust({ asset: new StellarSdk.LiquidityPoolAsset(eur, usd, 30), limit: "1000" })));
add("account_merge", builder()
  .addOperation(StellarSdk.Operation.accountMerge({ destination: pub })));
add("manage_data", builder()
  .addOperation(StellarSdk.Operation.manageData({ name: "key1", value: "hello world" })));
add("manage_data_delete", builder()
  .addOperation(StellarSdk.Operation.manageData({ name: "key2", value: null })));
add("bump_sequence", builder()
  .addOperation(StellarSdk.Operation.bumpSequence({ bumpTo: "999999999" })));
add("create_claimable_balance", builder()
  .addOperation(StellarSdk.Operation.createClaimableBalance({ asset: usd, amount: "5.5",
    claimants: [new StellarSdk.Claimant(pub, StellarSdk.Claimant.predicateUnconditional())] })));
add("clawback", builder()
  .addOperation(StellarSdk.Operation.clawback({ asset: usd, amount: "1", from: pub })));
add("clawback_claimable_balance", builder()
  .addOperation(StellarSdk.Operation.clawbackClaimableBalance({ balanceId: "00000000deadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeefdeadbeef" })));
add("begin_sponsoring", builder()
  .addOperation(StellarSdk.Operation.beginSponsoringFutureReserves({ sponsoredId: pub })));
add("end_sponsoring", builder()
  .addOperation(StellarSdk.Operation.endSponsoringFutureReserves({ source: pub })));
add("revoke_sponsorship_account", builder()
  .addOperation(StellarSdk.Operation.revokeAccountSponsorship({ account: pub })));
add("lp_deposit", builder()
  .addOperation(StellarSdk.Operation.liquidityPoolDeposit({ liquidityPoolId: StellarSdk.getLiquidityPoolId("constant_product", {fee: 30, assetA: eur, assetB: usd}), maxAmountA: "10", maxAmountB: "10", minPrice: { n: 1, d: 2 }, maxPrice: { n: 3, d: 4 } })));
add("lp_withdraw", builder()
  .addOperation(StellarSdk.Operation.liquidityPoolWithdraw({ liquidityPoolId: StellarSdk.getLiquidityPoolId("constant_product", {fee: 30, assetA: eur, assetB: usd}), amount: "5", minAmountA: "1", minAmountB: "1" })));
add("memo_text", builder().addMemo(StellarSdk.Memo.text("hello world"))
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })));
add("memo_id", builder().addMemo(StellarSdk.Memo.id("999999999999"))
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })));
add("memo_hash", builder().addMemo(StellarSdk.Memo.hash(Buffer.alloc(32,0xde)))
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })));
add("memo_return", builder().addMemo(StellarSdk.Memo["return"](Buffer.alloc(32,0xde)))
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })));
add("multi_op", builder()
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: usd, amount: "1" }))
  .addOperation(StellarSdk.Operation.manageData({ name: "k", value: "v" }))
  .addOperation(StellarSdk.Operation.payment({ destination: pub, asset: eur, amount: "2", source: pub })));
{
  const acc = new StellarSdk.Account(pub, "9999999999");
  const b = new StellarSdk.TransactionBuilder(acc, {
    fee: "100",
    networkPassphrase: passphrase,
    timebounds: { minTime: 1000000, maxTime: 2000000 },
    ledgerbounds: { minLedger: 500, maxLedger: 900 },
    minAccountSequenceAge: "3600",
    minAccountSequenceLedgerGap: 5,
    minAccountSequence: "1234567890123456789",
    extraSigners: [StellarSdk.StrKey.encodeSha256Hash(Buffer.alloc(32, 7))],
  });
  add("precond_v2",
      b.addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })));
}
// fee bump
{
  const inner = builder().addOperation(StellarSdk.Operation.payment({ destination: pub, asset: StellarSdk.Asset.native(), amount: "1" })).build();
  inner.sign(kp);
  const bump = StellarSdk.TransactionBuilder.buildFeeBumpTransaction(kp, "5000", inner, passphrase);
  bump.sign(kp);
  cases.push({ name: "fee_bump", envelopeXdr: bump.toEnvelope().toXDR("base64"), txHash: bump.hash().toString("hex") });
}

writeFileSync("/tmp/crossjs/fixtures_js.json", JSON.stringify({ seed, source: pub, cases }, null, 2));
console.log(`wrote ${cases.length} cases, source ${pub}`);
