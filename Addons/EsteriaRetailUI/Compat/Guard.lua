-- Records the stock globals before any retail file runs; Compat\Report.lua lists those the retail files replaced,
-- so a retail global that shadows a 3.3.5a one is found instead of breaking an unrelated stock frame.
EsteriaRetailUI = { stock = {} }
for name, value in pairs(_G) do
	if type(value) == "function" or type(value) == "table" then EsteriaRetailUI.stock[name] = value end
end
