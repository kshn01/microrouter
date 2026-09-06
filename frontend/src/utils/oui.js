/**
 * MicroRouter IEEE OUI (Organizationally Unique Identifier) Database
 * Maps MAC address prefixes (first 3 bytes / 6 hex digits) to device manufacturers and categories.
 */

export const IEEE_OUI = {
  // Smart TV & Media Renderers
  '10b232': { vendor: 'Hisense', type: 'Smart TV / Media', icon: 'tv' },
  '0005cd': { vendor: 'Denon / Marantz', type: 'Audio / Media Receiver', icon: 'speaker' },
  '001e65': { vendor: 'LG Electronics', type: 'LG Smart TV', icon: 'tv' },
  'a823fe': { vendor: 'LG Electronics', type: 'LG Smart TV', icon: 'tv' },
  '0026e8': { vendor: 'TCL Corporation', type: 'TCL Smart TV', icon: 'tv' },
  '00fc8b': { vendor: 'Amazon', type: 'FireTV / Echo', icon: 'tv' },
  '74c246': { vendor: 'Amazon', type: 'FireTV / Echo', icon: 'tv' },
  'f4d05c': { vendor: 'Amazon', type: 'FireTV Stick', icon: 'tv' },
  '000d4b': { vendor: 'Roku Inc.', type: 'Roku Player', icon: 'tv' },
  'b0ee45': { vendor: 'Roku Inc.', type: 'Roku Stick', icon: 'tv' },
  '0024be': { vendor: 'Sony', type: 'Sony Bravia TV', icon: 'tv' },
  '546009': { vendor: 'Google LLC', type: 'Chromecast / Nest', icon: 'tv' },
  '001d9b': { vendor: 'Panasonic', type: 'Viera TV', icon: 'tv' },

  // Gaming Consoles
  '000413': { vendor: 'Sony Interactive', type: 'PlayStation', icon: 'gamepad' },
  '00d009': { vendor: 'Sony Interactive', type: 'PlayStation 4/5', icon: 'gamepad' },
  'f8461c': { vendor: 'Sony Interactive', type: 'PlayStation 5', icon: 'gamepad' },
  '7ced8d': { vendor: 'Microsoft Corp', type: 'Xbox Series X/S', icon: 'gamepad' },
  'b4ae2b': { vendor: 'Microsoft Corp', type: 'Xbox Console', icon: 'gamepad' },
  '0009bf': { vendor: 'Nintendo Co.', type: 'Nintendo Console', icon: 'gamepad' },
  '98b6e9': { vendor: 'Nintendo Co.', type: 'Nintendo Switch', icon: 'gamepad' },
  'e0e751': { vendor: 'Nintendo Co.', type: 'Nintendo Switch', icon: 'gamepad' },

  // Smart Home & IoT
  '50c7bf': { vendor: 'TP-Link', type: 'Kasa Smart Plug/Bulb', icon: 'cpu' },
  '1869d8': { vendor: 'Tuya Smart', type: 'Smart Life IoT Plug', icon: 'cpu' },
  'd89c86': { vendor: 'Tuya Smart', type: 'Tuya Smart Home Node', icon: 'cpu' },
  '600194': { vendor: 'Itead / Sonoff', type: 'Sonoff Smart Switch', icon: 'cpu' },
  '34ea34': { vendor: 'Broadlink', type: 'Universal Remote', icon: 'cpu' },
  '840d8e': { vendor: 'Allterco / Shelly', type: 'Shelly Smart Relay', icon: 'cpu' },
  '7c49eb': { vendor: 'Xiaomi', type: 'Mi Smart Plug / Camera', icon: 'cpu' },
  'd0034b': { vendor: 'Apple Inc.', type: 'Apple HomePod / TV', icon: 'speaker' },

  // PC & Laptops
  '001b21': { vendor: 'Intel Corp', type: 'PC / Laptop', icon: 'laptop' },
  '00216b': { vendor: 'Intel Centrino', type: 'Laptop', icon: 'laptop' },
  '70f353': { vendor: 'Intel Wi-Fi 6', type: 'PC / Laptop', icon: 'laptop' },
  'a0c589': { vendor: 'Intel Wireless', type: 'PC / Laptop', icon: 'laptop' },
  '808600': { vendor: 'Intel Corp', type: 'PC / Laptop', icon: 'laptop' },
  '3c6a9d': { vendor: 'Intel Wi-Fi 6E', type: 'PC / Laptop', icon: 'laptop' },
  '00e04c': { vendor: 'Realtek', type: 'PC Network Adapter', icon: 'laptop' },
  '525400': { vendor: 'Realtek NIC', type: 'Virtual Machine / PC', icon: 'server' },
  '000acd': { vendor: 'Realtek', type: 'PC Network Adapter', icon: 'laptop' },
  '001422': { vendor: 'Dell Inc.', type: 'Dell PC / Laptop', icon: 'laptop' },
  'ecf4bb': { vendor: 'Dell Inc.', type: 'Dell Laptop', icon: 'laptop' },
  '000bcd': { vendor: 'HP Inc.', type: 'HP PC / Laptop', icon: 'laptop' },
  'a44c94': { vendor: 'Lenovo', type: 'Lenovo PC / ThinkPad', icon: 'laptop' },
  'c85b76': { vendor: 'Lenovo', type: 'Lenovo Laptop / Tablet', icon: 'laptop' },
  '086266': { vendor: 'ASUSTeK', type: 'ASUS PC / Laptop', icon: 'laptop' },
  '04d4c4': { vendor: 'ASUSTeK', type: 'ASUS Motherboard / PC', icon: 'laptop' },
  '002655': { vendor: 'Acer Inc.', type: 'Acer Laptop', icon: 'laptop' },
  '000c29': { vendor: 'VMware Inc.', type: 'Virtual Machine', icon: 'server' },
  '00155d': { vendor: 'Microsoft Hyper-V', type: 'Virtual Machine', icon: 'server' },
  '7c1e52': { vendor: 'Microsoft', type: 'Microsoft Surface', icon: 'laptop' },
  '0050f2': { vendor: 'Microsoft Corp', type: 'Windows Device', icon: 'laptop' },

  // Mobile Manufacturers
  '3c15c2': { vendor: 'Apple Inc.', type: 'iPhone / iPad', icon: 'smartphone' },
  '000393': { vendor: 'Apple Inc.', type: 'Apple Device', icon: 'smartphone' },
  '3c0754': { vendor: 'Apple Inc.', type: 'Mac / iPhone', icon: 'smartphone' },
  'a483e7': { vendor: 'Apple Inc.', type: 'iPhone', icon: 'smartphone' },
  'f099b6': { vendor: 'Apple Inc.', type: 'iPhone', icon: 'smartphone' },
  'fc257b': { vendor: 'Apple Inc.', type: 'iPad / Mac', icon: 'laptop' },
  'bcd1d3': { vendor: 'Samsung Electronics', type: 'Samsung Galaxy', icon: 'smartphone' },
  '002637': { vendor: 'Samsung Electronics', type: 'Samsung Galaxy', icon: 'smartphone' },
  '347c5c': { vendor: 'Samsung Electronics', type: 'Samsung Galaxy Phone', icon: 'smartphone' },
  '94b86d': { vendor: 'Samsung Electronics', type: 'Samsung Tablet', icon: 'tablet' },
  'ac5f3e': { vendor: 'Samsung Electronics', type: 'Samsung Galaxy', icon: 'smartphone' },
  '0c9150': { vendor: 'Xiaomi Communications', type: 'Xiaomi / Redmi Phone', icon: 'smartphone' },
  '14f65a': { vendor: 'Xiaomi Communications', type: 'Xiaomi / Poco Phone', icon: 'smartphone' },
  '286c07': { vendor: 'Xiaomi Communications', type: 'Xiaomi Smartphone', icon: 'smartphone' },
  '40313c': { vendor: 'Xiaomi Communications', type: 'Redmi / Poco Phone', icon: 'smartphone' },
  '7811dc': { vendor: 'Xiaomi Communications', type: 'Xiaomi Smartphone', icon: 'smartphone' },
  '28ed6a': { vendor: 'OnePlus / Oppo', type: 'Android Smartphone', icon: 'smartphone' },
  'c0eefb': { vendor: 'OnePlus', type: 'OnePlus Smartphone', icon: 'smartphone' },
  '4437e6': { vendor: 'BBK (Oppo/Vivo)', type: 'Oppo / Vivo Phone', icon: 'smartphone' },
  'ecd09f': { vendor: 'Oppo Mobile', type: 'Oppo Smartphone', icon: 'smartphone' },
  'a46c2a': { vendor: 'Vivo Mobile', type: 'Vivo Smartphone', icon: 'smartphone' },
  '1caf05': { vendor: 'Realme Mobile', type: 'Realme Smartphone', icon: 'smartphone' },
  '508c4a': { vendor: 'BBK (Realme)', type: 'Realme Smartphone', icon: 'smartphone' },
  '3c5ab4': { vendor: 'Google LLC', type: 'Google Pixel', icon: 'smartphone' },
  '000456': { vendor: 'Motorola Mobility', type: 'Motorola Phone', icon: 'smartphone' },
  '001a1b': { vendor: 'Motorola Mobility', type: 'Motorola Phone', icon: 'smartphone' },
  '001e10': { vendor: 'Huawei Technologies', type: 'Huawei Phone', icon: 'smartphone' },

  // Router / Gateway / Embedded
  'd4c1c8': { vendor: 'ZTE Corporation', type: 'Router / Gateway', icon: 'router' },
  '907069': { vendor: 'Espressif Systems', type: 'ESP32 IoT Node', icon: 'cpu' },
  '18fe34': { vendor: 'Espressif Systems', type: 'ESP8266 IoT Node', icon: 'cpu' },
  '240ac4': { vendor: 'Espressif Systems', type: 'ESP32 Node', icon: 'cpu' }
};

/**
 * Resolve vendor details from a MAC address string.
 * @param {string} mac - E.g. "90:70:69:AB:CD:EF" or "90-70-69-AB-CD-EF"
 * @returns {{ vendor: string, type: string, icon: string, isRandomized: boolean }}
 */
export function lookupOUI(mac) {
  if (!mac || typeof mac !== 'string') {
    return { vendor: 'Unknown', type: 'Network Device', icon: 'help', isRandomized: false };
  }

  const clean = mac.replace(/[:.-]/g, '').toLowerCase();
  if (clean.length < 6) {
    return { vendor: 'Invalid MAC', type: 'Network Device', icon: 'help', isRandomized: false };
  }

  // Check if MAC is locally administered (randomized MAC used by iOS / Android privacy)
  // Second hex digit has bit 1 set (2, 6, A, E)
  const firstByte = parseInt(clean.slice(0, 2), 16);
  const isRandomized = (firstByte & 0x02) !== 0;

  const prefix = clean.slice(0, 6);
  const match = IEEE_OUI[prefix];

  if (match) {
    return {
      vendor: match.vendor,
      type: match.type,
      icon: match.icon,
      isRandomized
    };
  }

  if (isRandomized) {
    return {
      vendor: 'Private MAC (Randomized)',
      type: 'Mobile / Laptop (Privacy)',
      icon: 'shield',
      isRandomized: true
    };
  }

  return {
    vendor: 'Unknown Vendor',
    type: 'Network Device',
    icon: 'wifi',
    isRandomized: false
  };
}
