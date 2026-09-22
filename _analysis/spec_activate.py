import json

print("=== views ===")
print(mcp.call("bn_binary_view_list"))
print("\n=== set active view_1 ===")
print(mcp.call("bn_binary_view_set_active", binaryView="view_1"))
print("\n=== active now ===")
print(mcp.call("bn_binary_view_get_active"))
