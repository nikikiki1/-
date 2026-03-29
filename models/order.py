import uuid
from datetime import datetime
from typing import Optional, Union
from models.product import Product
from services.product_manager import ProductManager


class Order:
    def __init__(self, customer_id: str, product_id: str, quantity: int, product_manager: Optional[ProductManager] = None):
        self._order_id: str = str(uuid.uuid4())
        self._customer_id: str = customer_id
        self._product_id: str = product_id
        self._quantity: int = quantity
        self._order_date: datetime = datetime.now()
        self._status: str = "pending"  # pending, accepted, completed, cancelled, rejected
        
        # 获取商品信息以计算总金额和商家ID
        self._product_manager = product_manager
        
        # 修复：完整的价格快照机制 - 记录订单创建时的商品完整信息
        self._initial_product_price: float = 0.0  # 快照单价
        self._initial_product_name: str = ""      # 快照商品名称
        self._initial_product_description: str = ""  # 快照商品描述
        self._initial_merchant_id: str = ""       # 快照商家ID
        
        # 如果有ProductManager，验证商品存在并获取初始信息
        if product_manager:
            product = product_manager.get_product(product_id)
            if product:
                self._merchant_id = product.merchant_id
                self._initial_merchant_id = product.merchant_id
                self._initial_product_price = product.price
                self._initial_product_name = product.name
                self._initial_product_description = product.description
                self._total_amount = self._initial_product_price * quantity
            else:
                self._merchant_id = None
                self._initial_merchant_id = ""
                self._total_amount = 0.0
        else:
            self._merchant_id = None
            self._initial_merchant_id = ""
            self._total_amount = 0.0
    
    @property
    def order_id(self) -> str:
        return self._order_id
    
    @property
    def customer_id(self) -> str:
        return self._customer_id
    
    @property
    def merchant_id(self) -> Optional[str]:
        return self._merchant_id
    
    @property
    def product_id(self) -> str:
        return self._product_id
    
    @property
    def quantity(self) -> int:
        return self._quantity
    
    @property
    def total_amount(self) -> float:
        return self._total_amount
    
    @property
    def status(self) -> str:
        return self._status
    
    @property
    def product_name(self) -> str:
        """动态获取商品名称"""
        if self._product_manager:
            product = self._product_manager.get_product(self._product_id)
            return product.name if product else "未知商品"
        return "未知商品"
    
    @property
    def product_price(self) -> float:
        """修复：返回订单创建时的快照价格，而非当前商品价格"""
        return self._initial_product_price
    
    @property
    def product_name(self) -> str:
        """修复：返回订单创建时的快照商品名称"""
        return self._initial_product_name
    
    @property
    def product_description(self) -> str:
        """修复：返回订单创建时的快照商品描述"""
        return self._initial_product_description
    
    @property
    def current_price(self) -> float:
        """获取当前商品价格（用于对比）"""
        if self._product_manager:
            product = self._product_manager.get_product(self._product_id)
            return product.price if product else 0.0
        return 0.0
    
    def refresh_product_info(self) -> None:
        """刷新商品信息（不影响快照）"""
        if self._product_manager:
            product = self._product_manager.get_product(self._product_id)
            if product:
                self._merchant_id = product.merchant_id
    
    @property
    def price(self) -> float:
        """修复：返回订单快照价格，与product_price一致"""
        return self._initial_product_price
    
    @property
    def order_date(self) -> datetime:
        return self._order_date
    
    def calculate_total(self) -> float:
        """计算订单总金额"""
        return self._total_amount
    
    def update_status(self, new_status: str) -> bool:
        """更新订单状态"""
        valid_transitions = {
            "pending": ["accepted", "cancelled", "rejected"],
            "accepted": ["completed"],
            "completed": [],
            "cancelled": [],
            "rejected": []
        }
        
        if new_status not in ["pending", "accepted", "completed", "cancelled", "rejected"]:
            return False
        
        if new_status in valid_transitions.get(self._status, []):
            self._status = new_status
            return True
        
        return False
    
    def complete(self) -> bool:
        """标记订单完成"""
        return self.update_status("completed")
    
    def cancel(self) -> bool:
        """取消订单"""
        return self.update_status("cancelled")
    
    def reject(self) -> bool:
        """拒绝订单"""
        return self.update_status("rejected")
    
    def accept(self) -> bool:
        """接受订单"""
        return self.update_status("accepted")
    
    def __str__(self) -> str:
        status_map = {
            "pending": "待处理",
            "accepted": "已接单",
            "completed": "已完成",
            "cancelled": "已取消",
            "rejected": "已拒绝"
        }
        
        # 修复：显示快照价格信息和价格变动提醒
        price_info = f"¥{self._initial_product_price}"
        if hasattr(self, '_current_price_checked') and self.current_price != self._initial_product_price:
            price_info += f" (当前价格: ¥{self.current_price})"
        
        return (f"订单ID: {self._order_id}\n" 
                f"商品: {self._initial_product_name}\n" 
                f"数量: {self._quantity}\n" 
                f"单价: {price_info}\n" 
                f"总金额: ¥{self._total_amount}\n" 
                f"状态: {status_map.get(self._status, self._status)}\n" 
                f"下单时间: {self._order_date.strftime('%Y-%m-%d %H:%M:%S')}")