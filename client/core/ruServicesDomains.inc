// CDN-fronted RU services that block/ degrade VPN users — routed direct by builtin-ru-direct.
// Source: v2fly domain-list-community (flattened: ozon, wildberries, avito, autoru, cian,
// flowwow, funpay, sber, tbank-ru, category-bank-ru, category-ecommerce-ru) + manual gov/travel.
// NOTE: unlike v2fly, the client resolves every entry as an exact host at connect time
// (vpnconnection.cpp) — no subdomain suffix matching — hence explicit www./m. variants.
// 509 domains total. Domains only; subnets stay in kRuDirectSubnets.

    // Ozon (marketplace + Ozon Bank) (63)
    "ad.ozone.ru", "adv.ozon.ru", "agency-adv.ozon.ru", "analytics-adv.ozon.ru", "beginners-adv.ozon.ru",
    "google-analytics-suite.ozon.ru", "loyalnost-adv.ozon.ru", "new-adv.ozon.ru", "o-courier.ru",
    "o3.ru", "o3t.ru", "o3team.ru", "ocourier.ru", "ozon-credit.ru", "ozon-dostavka.ru", "ozon-tech.ru", "ozon.app",
    "ozon.by", "ozon.com", "ozon.com.by", "ozon.com.kz", "ozon.dev", "ozon.express", "ozon.global", "ozon.kz",
    "ozon.li", "ozon.market", "ozon.ru", "ozon.tech", "ozon.tm", "ozon.travel", "ozoncamp.pro", "ozoncard.ru",
    "ozoncom.by", "ozoncorporate.ru", "ozone.ru", "ozonglobalevents.com", "ozonpartners.by", "ozonpartners.ru",
    "ozonrocet.by", "ozonroket.by", "ozonru.co.il", "ozonru.com", "ozonru.eu", "ozonru.me", "ozonsport.ru",
    "ozonstatus.ru", "ozonusercontent.com", "prodvizhenie-adv.ozon.ru", "questions-adv.ozon.ru", "rocketozon.by",
    "sentry.ozon.ru", "xapi.ozon.by", "xapi.ozon.com", "xapi.ozon.com.by", "xapi.ozon.com.kz", "xapi.ozon.kz",
    "xapi.ozon.ru", "xapi.ozonru.me", "xn----8sbahiqzthbd0bn.xn--p1ai", "xn--80aafgovrgbc7am.xn--p1ai",
    "xn--e1adjldbbpv.xn--90ais",

    // Wildberries (marketplace + WB Bank) (73)
    "ads-gate.wildberries.ru", "ads-media.wildberries.ru", "ads.wildberries.ru", "advert-api-sandbox.wb.ru",
    "advert-api-sandbox.wildberries.ru", "advert-api.wb.ru", "advert-api.wildberries.ru", "advert-media-api.wb.ru",
    "advert-media-api.wildberries.ru", "analytics-stage.wildberries.ru", "analytics.wildberries.ru", "digital-wb.ru",
    "digitalwb.ru", "etradebook.ru", "geobasket.ru", "marketing-info.wildberries.ru", "marketplace-sentry.wb.ru",
    "mp.world", "onlyjobs.ru", "paywb.com", "paywb.ru", "russoutdoor.ru", "rwb.ru", "rwbautomation.ru",
    "rwbautomation.tech", "rwbgeo.ru", "sentry.wb.ru", "vamzakaz.ru", "vsemrabota.ru", "warehouse.domains",
    "wb-bank.ru", "wb-basket.ru", "wb-cloud.ru", "wb-sale.ru", "wb.am", "wb.kg", "wb.kz", "wb.ru", "wbautomation.ru",
    "wbautomation.tech", "wbbasket.ru", "wbcontent.net", "wbdubna.ru", "wbkids.ru", "wbmail.ru", "wbmotivator.ru",
    "wbpay.ru", "wbr.by", "wbrep.ru", "wbstatic.net", "wbstatic.ru", "wbtek.ru", "wbwh.ru", "wbwh.tech",
    "web-analytics.wildberries.ru", "wibes.com", "wibes.ru", "wildberries.am", "wildberries.by", "wildberries.co.il",
    "wildberries.es", "wildberries.eu", "wildberries.ge", "wildberries.kg", "wildberries.kz", "wildberries.lt",
    "wildberries.ru", "wildberries.tj", "wildberries.tm", "wildberries.uz", "xn--80acbjgmges7ca.xn--p1ai",
    "xn--80acbjgsdq2cl.xn--p1ai", "xn--90ab.com",

    // Avito (classifieds) (3)
    "avito.ru", "avito.st", "stats.avito.ru",

    // Auto.ru (car classifieds) (5)
    "apiauto.ru", "auto.ru", "autoru.me", "avto.ru", "cm.expert",

    // Cian (real estate) (7)
    "cian.ru", "cian.site", "ciangroup.ru", "dmir.ru", "poudobnee.com", "xn--80aaafbpzn5blfby1p.xn--p1ai",
    "xn--80aaeib8abrryx4k.xn--p1ai",

    // Drom (car classifieds) (2)
    "drom.ru", "rdrom.ru",

    // Flowwow (flower delivery) (2)
    "flowwow-images.com", "flowwow.com",

    // FunPay (game goods marketplace) (3)
    "funpay.com", "funpay.ru", "sfunpay.com",

    // Sber (bank + ecosystem: Megamarket, Samokat, Kuper, CityDrive, SberCloud) (78)
    "21-school.ru", "accesspoint.uz", "ads.sber.ru", "afisha.ru", "bps-sberbank.by", "citydrive.ru",
    "clickstream.sberbank.ru", "cloud.ru", "cloudru.cn", "cplsb.ru", "eaptechka.online", "eapteka.ru",
    "error-tracking.megamarket.tech", "getsynapse.io", "giga.chat", "gitverse.ru", "id.ru", "jivo.chat", "jivo.ru",
    "jivochat.co.in", "jivochat.co.za", "jivochat.com", "jivochat.com.br", "jivochat.com.co", "jivochat.com.pe",
    "jivochat.com.tr", "jivochat.de", "jivochat.es", "jivochat.ng", "jivochat.ru", "jivosite.com", "jivosite.ru",
    "kuper.ru", "megamarket.ru", "megamarket.tech", "metrics.21-school.ru", "neon.click", "otello.ru", "payecom.ru",
    "platiecom.ru", "restnproducts.ru", "samokat.ru", "sber-bank.by", "sber.pro", "sber.ru", "sberauto.com",
    "sberbank-insurance.ru", "sberbank-tele.com", "sberbank.com", "sberbank.ru", "sberbankaktivno.ru",
    "sberbankins.ru", "sbercloud.ru", "sbercloud.tech", "sberdevices.ru", "sberdisk.ru", "sberfactoring.ru",
    "sberhealth.ru", "sberindex.ru", "sberleasing.ru", "sberlogistics.ru", "sbermarketing.ru", "sbermegamarket.ru",
    "sbermobile.ru", "sberpb.ru", "sberpro.vedomosti.ru", "sberspasibo.ru", "sbertech.ru", "sbertroika.ru",
    "sbrf.ru", "sentry.sberauto.com", "spasibo.digital", "spasibo.market", "spasibobonus.ru", "spasibosb.ru",
    "spasibosberbank.ru", "tracing-http.megamarket.tech", "visor.sberbank.ru",

    // T-Bank / Tinkoff / Rosbank (112)
    "2w.ru", "4c.ru", "4epenaxa.com", "atomyze.ru", "atomyze.tech", "babki.ru", "bankline.ru", "booking-tec.ru",
    "capitalt.ru", "cardsmobile.ru", "cdn-tinkoff.ru", "centraluniversity.ru", "codeseason.by", "cu.ru",
    "custody.ru", "data-tscbank.ru", "deltacredit.ru", "depository.ru", "dolyame.ru", "edu.tinkoff.ru",
    "education.tbank.ru", "electronicsignature.ru", "enterly.ru", "era-invest.ru", "etna.ai", "gainstech.ru",
    "gosdolgi.ru", "it-katok.ru", "it-picnic.ru", "it-piknik.ru", "jfn.su", "jira-tcsbank.ru", "jump.taxi",
    "jumpwork.ru", "karmahelp.ru", "karta-banka-rf.ru", "koshelek.app", "lifestyle-tec.ru", "malotogo.ru",
    "max-tcs.ru", "mfk-t-finance.ru", "my-tbank.ru", "phoenix-ca.ru", "private-banking.ru", "prodcontest.com",
    "rbcollect.ru", "rbcredit.ru", "rf-sim-mobile.ru", "rosbank-auto.ru", "rosbank-capital.ru", "rosbank-credit.com",
    "rosbank-dom.ru", "rosbank-leasing.ru", "rosbank.ru", "rusfinance.ru", "rusfinancebank.ru", "safeliner.tech",
    "sage-observability.tech", "sgcheck.ru", "t-access.ru", "t-bank-app.ru", "t-bank-app.su", "t-bank.by",
    "t-calls.ru", "t-capital-funds.ru", "t-cpa.ru", "t-dengi.ru", "t-finance.group", "t-finance.ru", "t-human.ru",
    "t-investlab.ru", "t-j.ru", "t-ofp.ru", "t-pulse.ru", "t-static.ru", "t-stekovka.ru", "t-tech.dev", "t-tech.ru",
    "t-tech.team", "t-technologies.ru", "t-tolk.ru", "t.finance", "tb-esim.ru", "tbank-online.com", "tbank.ru",
    "tconf.ru", "tcsbank.ru", "tdev.by", "time-messenger.ru", "tinkoff-edu.ru", "tinkoff-group.com", "tinkoff.ai",
    "tinkoff.ru", "tinkoffcapital.ru", "tinkoffinsurance.ru", "tinkoffinsurancefuture.ru", "tinkoffjournal.ru",
    "tinkoffmobile.com", "tinsurance.ru", "tinsurancefuture.ru", "tmiaf.ru", "tpension.ru", "travel-tec.ru",
    "unidraw.io", "xn----7sbhmncai5a0arm7c1e.xn--p1ai", "xn----8sbkfzespdwig.xn--p1ai", "xn----8sbwjbxyd.xn--p1ai",
    "xn--90aifd0az.shop", "xn--90aifd0azb.xn--p1acf", "xn--b1aechhj6bof8g5a.site", "xn--d1abkf4ap.fun",
    "xn--h1ao.site",

    // Other banks (v2fly category-bank-ru) (103)
    "abr.ru", "absolutbank.ru", "akbars.ru", "alefbank.com", "alfa.me", "alfabank.ru", "alfabank.st",
    "alfadirect.ru", "ankb.ru", "asia-pay.ru", "atb.su", "autokreditbank.ru", "avangard.ru", "aversbank.ru",
    "bank-arzamas.ru", "bank-credit-suisse-moscow.ru", "bank131.com", "bankermak.ru", "bbr.ru", "bcs-bank.ru",
    "bcs.ru", "best2pay.net", "bfa.ru", "blanc.ru", "bm-bank.ru", "broker.ru", "bspb.ru", "commerzbank.ru",
    "credit-zenit.ru", "dbmoscow.ru", "dbo-dengi.online", "deutsche-bank.ru", "deutschebank.ru", "domrfbank.ru",
    "finambank.ru", "forshtadt.ru", "gazprombank.com", "gazprombank.investments", "gazprombank.ru", "gid.ru",
    "gpb.ru", "homecredit.ru", "hva.im", "ibamoscow.ru", "jpmorgan.ru", "korona.com", "kubankredit.ru",
    "mfk-bank.ru", "mhcb.ru", "mhcbr.ru", "mkb.ru", "mtsbank.ru", "mtsdengi.ru", "mvs-bank.ru", "myapelsin.ru",
    "noosferabank.ru", "nspk.ru", "otpbank.ru", "payment.ru", "pochtabank.ru", "primbank.ru", "prvbank.ru",
    "psb-ocenka.ru", "psb.ru", "psbank.ru", "pskb.ru", "pvubank.com", "raiffeisen-media.ru", "raiffeisen.ru",
    "rbsgate.com", "rbsuat.com", "rencapbank.ru", "rencredit.ru", "rncoluminis.ru", "rshb.ru", "ru.ccb.com",
    "sbibankllc.ru", "seb.ru", "selkombank.ru", "sinara.ru", "slavbank.ru", "smbcr-bank.ru", "solid.ru",
    "sovcombank.ru", "ssb.msk.ru", "tagbank.ru", "tatsotsbank.ru", "tkbbank.ru", "tochka-tech.com", "tochka.com",
    "uralsib.ru", "vlb100.ru", "vtb-direct.com", "vtb-grants.fut.ru", "vtb.ru",
    "vtbglobalperspectives.com", "westernunion.ru", "xn--d1aqf.xn--p1ai", "yoobusiness.ru", "zenit-card.ru",
    "zenit.ru", "ziraatbank.ru",

    // Government services (manual: gosuslugi, FNS nalog, mos.ru) (9)
    "esia.gosuslugi.ru", "gosuslugi.ru", "gu-st.ru", "mos.ru", "nalog.gov.ru", "nalog.ru", "npd.nalog.ru",
    "www.gosuslugi.ru", "www.mos.ru",

    // Travel & transport (manual: RZD, Ostrovok, Tutu.ru) (7)
    "ostrovok.ru", "pass.rzd.ru", "rzd.ru", "tutu.ru", "www.ostrovok.ru", "www.rzd.ru", "www.tutu.ru",

    // Canonical www./m. host variants (exact-host resolution) (42)
    "m.alfabank.ru", "m.auto.ru", "m.avito.ru", "m.cian.ru", "m.drom.ru", "m.gazprombank.ru", "m.kuper.ru",
    "m.megamarket.ru", "m.mtsbank.ru", "m.ozon.ru", "m.ozone.ru", "m.psb.ru", "m.raiffeisen.ru", "m.rshb.ru",
    "m.samokat.ru", "m.sberbank.ru", "m.sovcombank.ru", "m.tbank.ru", "m.tinkoff.ru", "m.vtb.ru", "m.wildberries.ru",
    "www.alfabank.ru", "www.auto.ru", "www.avito.ru", "www.cian.ru", "www.drom.ru", "www.gazprombank.ru",
    "www.kuper.ru", "www.megamarket.ru", "www.mtsbank.ru", "www.ozon.ru", "www.ozone.ru", "www.psb.ru",
    "www.raiffeisen.ru", "www.rshb.ru", "www.samokat.ru", "www.sberbank.ru", "www.sovcombank.ru", "www.tbank.ru",
    "www.tinkoff.ru", "www.vtb.ru", "www.wildberries.ru",
