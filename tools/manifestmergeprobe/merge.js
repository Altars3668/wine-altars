var dir = WScript.Arguments(0), progid = WScript.Arguments.length > 1 ? WScript.Arguments(1) : "Msxml2.DOMDocument.6.0";
var ns = "xmlns:a='http://schemas.microsoft.com/appx/2010/manifest' xmlns:appv='http://schemas.microsoft.com/appv/2010/manifest'";
function load(name) {
    var d = new ActiveXObject(progid);
    d.async = false;
    if (progid.indexOf("6.0") < 0) d.setProperty("SelectionLanguage", "XPath");
    d.setProperty("SelectionNamespaces", ns);
    if (!d.load(dir + "\\" + name)) WScript.Echo("load failed " + name + ": " + d.parseError.reason);
    return d;
}
function show(label, node) {
    var x = node.xml;
    WScript.Echo(label + ": nodeName=" + node.nodeName + " prefix=" + node.prefix + " ns=" + node.namespaceURI + " xml=" + x.substring(0, 110));
}
var modes = ["move", "clone", "import"];
for (var m = 0; m < modes.length; m++) {
    var main = load("AppXManifest.common.16.xml");
    var prod = load("AppXManifest.90160000-0016-0000-1000-0000000FF1CE.xml");
    var target = main.selectSingleNode("/a:Package/appv:Extensions");
    var exts = prod.selectNodes("/a:Package/appv:Extensions/appv:Extension");
    WScript.Echo("== " + modes[m] + " (" + progid + "): target " + (target ? target.nodeName : "null") + ", " + exts.length + " product extensions");
    var ext = exts.item(0), added;
    if (modes[m] == "move") added = target.appendChild(ext);
    else if (modes[m] == "clone") added = target.appendChild(ext.cloneNode(true));
    else { try { added = target.appendChild(main.importNode(ext, true)); } catch (e) { WScript.Echo("importNode: " + e.message); continue; } }
    show("appended", added);
    var x = main.xml, i = x.indexOf("Excel.CSV");
    WScript.Echo("serialized: " + x.substring(x.lastIndexOf("<", x.lastIndexOf("FileTypeAssociation", i) - 2) - 40, i + 10).replace(/\s+/g, " "));
    var re = new ActiveXObject(progid); re.async = false; re.setProperty("SelectionNamespaces", ns);
    re.loadXML(x);
    WScript.Echo("reparsed appv:Extension count = " + re.selectNodes("/a:Package/appv:Extensions/appv:Extension").length + ", a:Extension count = " + re.selectNodes("/a:Package/appv:Extensions/a:Extension").length);
}
