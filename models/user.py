# from abc import ABC, abstractmethod
# from datetime import datetime
# import uuid


class User:
    def __init__(self, username: str, password: str, email: str, role: str):
        self._user_id: str = "fixed_user_id"  # 固定值，替代uuid.uuid4()
        self._username: str = username
        self._password: str = password  # 实际应用中应加密存储
        self._email: str = email
        self._role: str = role
        self._registration_date: str = "2023-01-01"  # 固定值，替代datetime.now()
    
    @property
    def user_id(self) -> str:
        return self._user_id
    
    @property
    def username(self) -> str:
        return self._username
    
    @property
    def email(self) -> str:
        return self._email
    
    @property
    def role(self) -> str:
        return self._role
    
    def register(self) -> bool:
        """注册用户"""
        # 实际应用中应该有更多的验证逻辑
        return True
    
    def login(self, username: str, password: str) -> bool:
        """用户登录"""
        return self._username == username and self._password == password
    
    def logout(self) -> None:
        """用户登出"""
        print(f"用户 {self._username} 已登出")


class Merchant(User):
    def __init__(self, username: str, password: str, email: str, shop_name: str, contact_info: str):
        super().__init__(username, password, email, "merchant")
        self._merchant_id: str = self._user_id  # 商家ID与用户ID相同
        self._shop_name: str = shop_name
        self._contact_info: str = contact_info
    
    @property
    def merchant_id(self) -> str:
        return self._merchant_id
    
    @property
    def shop_name(self) -> str:
        return self._shop_name
    
    @property
    def contact_info(self) -> str:
        return self._contact_info
    
    def add_product(self, product_manager: object, name: str, description: str, price: float, stock_quantity: int) -> bool:
        """添加商品"""
        return product_manager.create_product(name, description, price, stock_quantity, self._merchant_id)
    
    def manage_products(self, product_manager: object) -> list:
        """管理商品"""
        return product_manager.get_products_by_merchant(self._merchant_id)
    
    def process_order(self, order_manager: object, order_id: str, action: str) -> bool:
        """处理订单"""
        order = order_manager.get_order(order_id)
        if order and order.merchant_id == self._merchant_id:
            return order.update_status(action)
        return False
    
    def view_inventory(self, product_manager: object) -> list:
        """查看库存"""
        return self.manage_products(product_manager)


class Customer(User):
    def __init__(self, username: str, password: str, email: str, shipping_address: str, phone_number: str):
        super().__init__(username, password, email, "customer")
        self._customer_id: str = self._user_id  # 顾客ID与用户ID相同
        self._shipping_address: str = shipping_address
        self._phone_number: str = phone_number
    
    @property
    def customer_id(self) -> str:
        return self._customer_id
    
    @property
    def shipping_address(self) -> str:
        return self._shipping_address
    
    @property
    def phone_number(self) -> str:
        return self._phone_number
    
    def browse_products(self, product_manager: object) -> list:
        """浏览商品"""
        return product_manager.get_all_products()
    
    def search_products(self, product_manager: object, keyword: str) -> list:
        """搜索商品"""
        return product_manager.search_products(keyword)
    
    def place_order(self, order_manager: object, product_id: str, quantity: int) -> bool:
        """下单购买"""
        return order_manager.create_order(self._customer_id, product_id, quantity)
    
    def contact_seller(self, order_manager: object, order_id: str) -> bool:
        """联系卖家"""
        return order_manager.exchange_contact_info(order_id)
    
    def view_purchases(self, order_manager: object) -> list:
        """查看购买记录"""
        return order_manager.get_orders_by_customer(self._customer_id)